#include "plynth/core/EditSession.h"
#include "plynth/core/Process.h"
#include "plynth/core/ProjectFiles.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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

// The inherited environment with the given entries added or replaced, as one block for CreateProcess.
std::wstring environmentBlock(const std::vector<std::pair<std::string, std::string>>& extra)
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

    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (job.valid()) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits));
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
    if (!spec.environment.empty()) env = environmentBlock(spec.environment);
    const std::wstring workingFolder = spec.workingFolder.wstring();

    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    if (attributes.ready) flags |= EXTENDED_STARTUPINFO_PRESENT;
    PROCESS_INFORMATION info{};
    const BOOL started = CreateProcessW(spec.program.c_str(), commandLine.data(), nullptr, nullptr, TRUE, flags,
                                        env.empty() ? nullptr : env.data(),
                                        workingFolder.empty() ? nullptr : workingFolder.c_str(),
                                        &startup.StartupInfo, &info);
    if (!started) {
        result.status = fail(ErrorCode::CannotRead, "The program " + pathToUtf8(spec.program) + " could not be started.");
        return result;
    }
    Handle process(info.hProcess);
    Handle thread(info.hThread);
    if (job.valid()) AssignProcessToJobObject(job.get(), process.get());
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
    } else {
        if (job.valid()) TerminateJobObject(job.get(), 1);
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
