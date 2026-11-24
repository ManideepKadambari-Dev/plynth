#include "plynth/core/EditSession.h"
#include "plynth/core/Process.h"
#include "plynth/core/ProjectFiles.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <memory>
#include <system_error>

namespace plynth {

namespace {

// Reads the creation time of a process as one number. False when Windows refuses.
bool creationKey(HANDLE process, long long& key)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return false;
    ULARGE_INTEGER value;
    value.LowPart = created.dwLowDateTime;
    value.HighPart = created.dwHighDateTime;
    key = static_cast<long long>(value.QuadPart);
    return true;
}

// Turns UTF-8 text into UTF-16.
std::wstring widen(const std::string& text)
{
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// Quotes one argument the way the Windows C runtime reads it back.
std::wstring quoteArgument(const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

// Compares environment names the way Windows does, ignoring case.
struct EnvNameLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) ==
               CSTR_LESS_THAN;
    }
};

// The inherited environment without the removed names and with the given entries added or replaced, as one
// block for CreateProcess.
std::wstring environmentBlock(const std::vector<std::pair<std::string, std::string>>& extra,
                              const std::vector<std::string>& removed)
{
    std::map<std::wstring, std::wstring, EnvNameLess> vars;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p;) {
            const std::wstring entry(p);
            p += entry.size() + 1;
            const std::size_t eq = entry.find(L'=', 1);
            if (eq == std::wstring::npos) continue;
            vars[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    for (const std::string& name : removed) vars.erase(widen(name));
    for (const auto& [name, value] : extra) vars[widen(name)] = widen(value);
    std::wstring out;
    for (const auto& [name, value] : vars) {
        out += name;
        out += L'=';
        out += value;
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

// Closes a Windows handle when it goes out of scope.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return h_; }
    bool valid() const { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE h = nullptr)
    {
        if (valid()) CloseHandle(h_);
        h_ = h;
    }

private:
    HANDLE h_ = nullptr;
};

// Frees a process attribute list when it goes out of scope.
struct AttributeList {
    std::unique_ptr<char[]> memory;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    bool ready = false;
    ~AttributeList()
    {
        if (ready) DeleteProcThreadAttributeList(list);
    }
};

// Cuts text into lines and passes each one on. Keeps a partial line until the rest arrives.
class LineSplitter {
public:
    explicit LineSplitter(const std::function<void(std::string_view)>& onLine) : onLine_(onLine) {}

    // Takes more output.
    void add(const char* data, std::size_t size)
    {
        pending_.append(data, size);
        std::size_t start = 0;
        for (std::size_t nl; (nl = pending_.find('\n', start)) != std::string::npos; start = nl + 1) {
            std::size_t end = nl;
            if (end > start && pending_[end - 1] == '\r') --end;
            emit(std::string_view(pending_).substr(start, end - start));
        }
        pending_.erase(0, start);
    }

    // Passes on the last line when the output did not end with a line break.
    void finish()
    {
        if (!pending_.empty()) emit(pending_);
        pending_.clear();
    }

private:
    void emit(std::string_view line)
    {
        if (onLine_) onLine_(line);
    }

    const std::function<void(std::string_view)>& onLine_;
    std::string pending_;
};

// Room for the Windows id of the low integrity level, S-1-16-4096.
struct LowLabelSid {
    alignas(DWORD) BYTE bytes[SECURITY_MAX_SID_SIZE]{};
    bool ok = false;
    LowLabelSid()
    {
        DWORD size = sizeof(bytes);
        ok = CreateWellKnownSid(WinLowLabelSid, nullptr, bytes, &size) != FALSE;
    }
    PSID get() { return bytes; }
};

// Makes a copy of this program's sign in with its extra rights taken away, lowered to low integrity, for
// starting a program with. False when Windows refuses any step.
bool makeLowToken(Handle& out)
{
    HANDLE own = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, &own)) {
        return false;
    }
    Handle ownToken(own);
    HANDLE restricted = nullptr;
    if (!CreateRestrictedToken(own, DISABLE_MAX_PRIVILEGE, 0, nullptr, 0, nullptr, 0, nullptr, &restricted)) return false;
    out.reset(restricted);
    LowLabelSid sid;
    if (!sid.ok) return false;
    TOKEN_MANDATORY_LABEL label{};
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    label.Label.Sid = sid.get();
    return SetTokenInformation(out.get(), TokenIntegrityLevel, &label, sizeof(label) + GetLengthSid(sid.get())) != FALSE;
}

// Says whether a process runs at low integrity, read back from its own sign in.
bool runsAtLowIntegrity(HANDLE process)
{
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) return false;
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenIntegrityLevel, nullptr, 0, &size);
    if (size == 0) return false;
    std::vector<DWORD> buffer(size / sizeof(DWORD) + 1);
    if (!GetTokenInformation(token.get(), TokenIntegrityLevel, buffer.data(), size, &size)) return false;
    const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
    const PSID level = label->Label.Sid;
    if (!IsValidSid(level) || *GetSidSubAuthorityCount(level) == 0) return false;
    return *GetSidSubAuthority(level, *GetSidSubAuthorityCount(level) - 1u) == SECURITY_MANDATORY_LOW_RID;
}

// Opens every process the job lists that is not open yet. True when one was added.
bool openJobProcesses(HANDLE job, std::map<DWORD, Handle>& open)
{
    constexpr DWORD kMaxIds = 4096;
    std::vector<ULONG_PTR> buffer(kMaxIds + 2, 0);
    auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(buffer.size() * sizeof(ULONG_PTR)),
                                   nullptr) &&
        GetLastError() != ERROR_MORE_DATA) {
        return false;
    }
    bool added = false;
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList && i < kMaxIds; ++i) {
        const auto pid = static_cast<DWORD>(list->ProcessIdList[i]);
        if (open.count(pid) != 0) continue;
        const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) continue;
        open.try_emplace(pid, process);
        added = true;
    }
    return added;
}

// Ends every process in the job and waits until all of them are gone. False when some still run after a
// minute.
bool endJob(HANDLE job)
{
    // The job's own count drops while a process is still ending, so each process is opened before it is
    // ended and its handle is waited on.
    const auto end = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    std::map<DWORD, Handle> open;
    for (;;) {
        const bool added = openJobProcesses(job, open);
        TerminateJobObject(job, 1);
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        bool running = !QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr) ||
                       info.ActiveProcesses != 0;
        for (const auto& [pid, process] : open) {
            if (WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0) running = true;
        }
        if (!running && !added) return true;
        if (std::chrono::steady_clock::now() >= end) return false;
        Sleep(10);
    }
}

// Says whether a list of labels holds the low integrity label that new files and folders inherit, and that
// stops low programs only from writing to things labelled higher.
bool isLowLabel(PACL sacl)
{
    LowLabelSid low;
    for (DWORD i = 0; low.ok && sacl && i < sacl->AceCount; ++i) {
        void* entry = nullptr;
        if (!GetAce(sacl, i, &entry)) return false;
        const auto* header = static_cast<const ACE_HEADER*>(entry);
        if (header->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;
        auto* ace = static_cast<SYSTEM_MANDATORY_LABEL_ACE*>(entry);
        const BYTE inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
        return EqualSid(reinterpret_cast<PSID>(&ace->SidStart), low.get()) &&
               (ace->Mask & SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) != 0 && (header->AceFlags & inherit) == inherit &&
               (header->AceFlags & INHERIT_ONLY_ACE) == 0;
    }
    return false;
}

// Says whether a folder carries the low integrity label.
bool hasLowLabel(const std::filesystem::path& folder)
{
    PACL sacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::wstring name = folder.wstring();
    if (GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr,
                              &sacl, &descriptor) != ERROR_SUCCESS) {
        return false;
    }
    const bool found = isLowLabel(sacl);
    LocalFree(descriptor);
    return found;
}

// Says whether a security id is this program's user, or the owner it gives the files it makes.
bool isCurrentUser(PSID owner)
{
    HANDLE raw = nullptr;
    if (!owner || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return false;
    Handle token(raw);
    for (const TOKEN_INFORMATION_CLASS kind : {TokenUser, TokenOwner}) {
        DWORD size = 0;
        GetTokenInformation(token.get(), kind, nullptr, 0, &size);
        if (size == 0) continue;
        std::vector<DWORD> buffer(size / sizeof(DWORD) + 1);
        if (!GetTokenInformation(token.get(), kind, buffer.data(), size, &size)) continue;
        const PSID sid = kind == TokenUser ? reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid
                                           : reinterpret_cast<const TOKEN_OWNER*>(buffer.data())->Owner;
        if (EqualSid(sid, owner)) return true;
    }
    return false;
}

}

// The id and creation time of this process.
ProcessIdentity currentProcess()
{
    ProcessIdentity identity;
    identity.pid = static_cast<long long>(GetCurrentProcessId());
    creationKey(GetCurrentProcess(), identity.startKey);
    return identity;
}

// Says whether that exact run of a process is still going.
bool isProcessRunning(const ProcessIdentity& process)
{
    if (process.pid <= 0 || process.pid > 0xFFFFFFFFLL) return false;
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(process.pid));
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED;

    bool running = false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(handle, &exitCode) && exitCode == STILL_ACTIVE) {
        running = true;
        if (process.startKey != 0) {
            long long key = 0;
            running = creationKey(handle, key) && key == process.startKey;
        }
    }
    CloseHandle(handle);
    return running;
}

// Runs a program to its end and reads its output as lines.
ProcessResult runProcess(const ProcessSpec& spec, const std::function<void(std::string_view line)>& onLine,
                         std::stop_token stop)
{
    ProcessResult result;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(spec.program, ec)) {
        result.status = fail(ErrorCode::NotFound, "The program " + pathToUtf8(spec.program) + " was not found.");
        return result;
    }

    std::wstring commandLine = quoteArgument(spec.program.wstring());
    for (const std::string& arg : spec.arguments) {
        commandLine += L' ';
        commandLine += quoteArgument(widen(arg));
    }

    Handle lowToken;
    if (spec.lowIntegrity && !makeLowToken(lowToken)) {
        result.status = fail(ErrorCode::CannotRead,
                             "The program " + pathToUtf8(spec.program) + " could not be set up to run at low integrity.");
        return result;
    }

    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle readEnd;
    Handle writeEnd;
    {
        HANDLE r = nullptr;
        HANDLE w = nullptr;
        if (!CreatePipe(&r, &w, &inheritable, 0)) {
            result.status = fail(ErrorCode::CannotRead, "A pipe for the program output could not be created.");
            return result;
        }
        readEnd.reset(r);
        writeEnd.reset(w);
    }
    SetHandleInformation(readEnd.get(), HANDLE_FLAG_INHERIT, 0);
    Handle nul(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr));

    // A low integrity run or one that must end its children needs the job, so it is not started without one.
    const bool needsJob = spec.lowIntegrity || spec.endChildren;
    Handle job(CreateJobObjectW(nullptr, nullptr));
    bool jobReady = false;
    if (job.valid()) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        jobReady = SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE;
    }
    if (needsJob && !jobReady) {
        result.status = fail(ErrorCode::CannotRead,
                             "The program " + pathToUtf8(spec.program) + " could not be started in a job of its own.");
        return result;
    }

    // Only the pipe and the empty input are handed to the child, so another thread starting a program at the
    // same moment cannot leak this pipe into it.
    HANDLE inherited[2] = {writeEnd.get(), nul.valid() ? nul.get() : writeEnd.get()};
    AttributeList attributes;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    attributes.memory = std::make_unique<char[]>(attributeBytes);
    attributes.list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.memory.get());
    if (InitializeProcThreadAttributeList(attributes.list, 1, 0, &attributeBytes)) {
        attributes.ready = true;
        UpdateProcThreadAttribute(attributes.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                  nul.valid() ? sizeof(inherited) : sizeof(HANDLE), nullptr, nullptr);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = attributes.ready ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = writeEnd.get();
    startup.StartupInfo.hStdError = writeEnd.get();
    startup.StartupInfo.hStdInput = nul.valid() ? nul.get() : nullptr;
    if (attributes.ready) startup.lpAttributeList = attributes.list;

    std::wstring env;
    if (!spec.environment.empty() || !spec.removeEnvironment.empty()) {
        env = environmentBlock(spec.environment, spec.removeEnvironment);
    }
    const std::wstring workingFolder = spec.workingFolder.wstring();

    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    if (attributes.ready) flags |= EXTENDED_STARTUPINFO_PRESENT;
    PROCESS_INFORMATION info{};
    LPVOID envBlock = env.empty() ? nullptr : env.data();
    LPCWSTR folder = workingFolder.empty() ? nullptr : workingFolder.c_str();
    const BOOL started = spec.lowIntegrity
                             ? CreateProcessAsUserW(lowToken.get(), spec.program.c_str(), commandLine.data(), nullptr,
                                                    nullptr, TRUE, flags, envBlock, folder, &startup.StartupInfo, &info)
                             : CreateProcessW(spec.program.c_str(), commandLine.data(), nullptr, nullptr, TRUE, flags,
                                              envBlock, folder, &startup.StartupInfo, &info);
    if (!started) {
        result.status = fail(ErrorCode::CannotRead, "The program " + pathToUtf8(spec.program) + " could not be started.");
        return result;
    }
    Handle process(info.hProcess);
    Handle thread(info.hThread);
    const bool inJob = jobReady && AssignProcessToJobObject(job.get(), process.get());
    // The program is still paused here, so ending it now means none of its own code has run.
    if ((needsJob && !inJob) || (spec.lowIntegrity && !runsAtLowIntegrity(process.get()))) {
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), 5000);
        result.status = fail(ErrorCode::CannotRead, "The program " + pathToUtf8(spec.program) +
                                                        " could not be started with the protection it was asked for.");
        return result;
    }
    ResumeThread(thread.get());
    writeEnd.reset();
    nul.reset();

    LineSplitter lines(onLine);
    char buffer[8192];
    const auto begin = std::chrono::steady_clock::now();
    bool ended = false;

    // Reads whatever is waiting in the pipe. False when nothing was waiting.
    const auto drain = [&]() {
        bool any = false;
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(readEnd.get(), nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
            DWORD got = 0;
            if (!ReadFile(readEnd.get(), buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr) || got == 0) break;
            lines.add(buffer, got);
            result.outputTail.append(buffer, got);
            if (result.outputTail.size() > 2 * kProcessOutputTailBytes) {
                result.outputTail.erase(0, result.outputTail.size() - kProcessOutputTailBytes);
            }
            any = true;
        }
        return any;
    };

    while (!ended) {
        const bool any = drain();
        if (stop.stop_requested()) break;
        if (spec.timeout.count() > 0 && std::chrono::steady_clock::now() - begin >= spec.timeout) {
            result.timedOut = true;
            break;
        }
        if (!any && WaitForSingleObject(process.get(), 10) == WAIT_OBJECT_0) ended = true;
    }

    if (ended) {
        drain();
        DWORD code = 0;
        if (GetExitCodeProcess(process.get(), &code)) result.exitCode = static_cast<int>(code);
        if (spec.endChildren) {
            const bool gone = endJob(job.get());
            drain();
            if (!gone) result.status = fail(ErrorCode::CannotRead, "Programs that the program started could not all be ended.");
        } else if (inJob) {
            TerminateJobObject(job.get(), 1);
        }
    } else {
        if (inJob) endJob(job.get());
        else TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), 5000);
        drain();
        result.status = fail(ErrorCode::Cancelled, result.timedOut ? "The program ran out of time and was stopped."
                                                                    : "The program was stopped.");
    }
    lines.finish();
    if (result.outputTail.size() > kProcessOutputTailBytes) {
        result.outputTail.erase(0, result.outputTail.size() - kProcessOutputTailBytes);
    }
    return result;
}

// Makes a folder, or labels an empty one, so a low integrity program may write in it and in what it makes
// there, then reads the label back to be sure it is set.
Status makeLowIntegrityFolder(const std::filesystem::path& folder)
{
    namespace fs = std::filesystem;
    const auto refuse = [&](const std::string& why) {
        return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(folder) + " " + why);
    };
    if (folder.empty()) return fail(ErrorCode::CannotWrite, "No folder was given for low integrity programs.");

    LowLabelSid low;
    std::vector<DWORD> aclBuffer((sizeof(ACL) + sizeof(SYSTEM_MANDATORY_LABEL_ACE) + SECURITY_MAX_SID_SIZE) / sizeof(DWORD) + 1);
    auto* acl = reinterpret_cast<PACL>(aclBuffer.data());
    SECURITY_DESCRIPTOR descriptor{};
    const bool built = low.ok && InitializeAcl(acl, static_cast<DWORD>(aclBuffer.size() * sizeof(DWORD)), ACL_REVISION) &&
                       AddMandatoryAce(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                       SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, low.get()) &&
                       InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
                       SetSecurityDescriptorSacl(&descriptor, TRUE, acl, FALSE);
    if (!built) return refuse("could not be labelled, because the label could not be built.");

    std::error_code ec;
    const fs::file_status state = fs::symlink_status(folder, ec);
    bool made = false;
    if (state.type() == fs::file_type::not_found) {
        const fs::path parent = folder.parent_path();
        if (!parent.empty()) fs::create_directories(parent, ec);
        if (ec) return refuse("could not be made.");
        // Made with the label already in place, so it never exists without it.
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
        if (!CreateDirectoryW(folder.c_str(), &attributes)) return refuse("could not be made.");
        made = true;
    } else {
        const DWORD attributes = GetFileAttributesW(folder.c_str());
        if (ec || state.type() != fs::file_type::directory || attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return refuse("is not a plain folder.");
        }
        if (!fs::is_empty(folder, ec) || ec) return refuse("is not empty.");
    }

    if (!hasLowLabel(folder)) {
        std::wstring name = folder.wstring();
        SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, acl);
        if (!hasLowLabel(folder)) {
            if (made) RemoveDirectoryW(folder.c_str());
            return refuse("could not be labelled for low integrity programs.");
        }
    }
    return {};
}

// Checks that a kept folder for low integrity programs is a real folder of this user with the low label,
// reading it only.
Status checkLowIntegrityFolder(const std::filesystem::path& folder)
{
    const Status notSetUp = fail(ErrorCode::CannotWrite, "Plynth's fetch folder is not set up as expected. Delete it and try again.");
    if (folder.empty()) return notSetUp;
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return fail(ErrorCode::NotFound, "The folder for low integrity programs was not found.");
        }
        return notSetUp;
    }
    // Opened without following a link, so what is checked is the folder itself and not where a link points.
    Handle handle(CreateFileW(folder.c_str(), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!handle.valid() || !GetFileInformationByHandle(handle.get(), &info)) return notSetUp;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return notSetUp;
    }
    PSID owner = nullptr;
    PACL sacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle.get(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, &owner,
                        nullptr, nullptr, &sacl, &descriptor) != ERROR_SUCCESS) {
        return notSetUp;
    }
    const bool ok = isCurrentUser(owner) && isLowLabel(sacl);
    LocalFree(descriptor);
    return ok ? Status{} : notSetUp;
}

// Moves a file or folder to the Recycle Bin.
Status moveToRecycleBin(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return fail(ErrorCode::NotFound, "The path " + pathToUtf8(path) + " was not found.");
    }
    std::filesystem::path full = std::filesystem::absolute(path, ec);
    if (ec) full = path;
    full.make_preferred();
    std::wstring from = full.wstring();
    from.push_back(L'\0');
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int code = SHFileOperationW(&op);
    if (code != 0 || op.fAnyOperationsAborted) {
        return fail(ErrorCode::CannotWrite, "The path " + pathToUtf8(path) + " could not be moved to the Recycle Bin.");
    }
    return {};
}

}
