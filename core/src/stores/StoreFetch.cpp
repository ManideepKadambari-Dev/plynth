#include "StoreFetchFolder.h"

#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/BlenderKit.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <system_error>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Reads a text field of an object. A null counts as empty when allowNull is set.
bool textField(const Json& object, const char* name, std::string& out, bool allowNull)
{
    if (!object.is_object() || !object.contains(name)) return false;
    const Json& value = object[name];
    if (value.is_null()) return allowNull;
    if (!value.is_string()) return false;
    out = value.get<std::string>();
    return true;
}

// True when a relative file name stays inside its folder.
bool staysInside(const std::string& name)
{
    if (name.empty() || name.find('\\') != std::string::npos) return false;
    const fs::path path = pathFromUtf8(name);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    return std::none_of(path.begin(), path.end(), [](const fs::path& part) { return part == ".."; });
}

// The refusal sentence for an item of the wrong kind.
std::string wrongKindSentence(const ImportFetch& fetch, std::string_view fetchedKind)
{
    const std::string store = fetch.task == kBlenderKitFetchTask ? "BlenderKit item" : "item";
    if (fetchedKind == kFetchKindMaterial) {
        return "This " + store + " is a material. Import it in the Materials window or with plynth materials import.";
    }
    return "This " + store + " is a model. Import it in the Assets window or with plynth assets import.";
}

const char* const kFolderNotSetUp = "Plynth's fetch folder is not set up as expected. Delete it and try again.";

// The same path in the form Windows reads without the old length limit, because a store's download can
// nest deeper than that.
fs::path longPath(const fs::path& path)
{
    fs::path plain = path.lexically_normal();
    plain.make_preferred();
    const std::wstring text = plain.wstring();
    if (text.rfind(L"\\\\?\\", 0) == 0) return plain;
    if (text.rfind(L"\\\\", 0) == 0) return fs::path(L"\\\\?\\UNC\\" + text.substr(2));
    return fs::path(L"\\\\?\\" + text);
}

// The files and folders directly in a folder.
std::vector<fs::path> entriesOf(const fs::path& folder)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) out.push_back(it->path());
    return out;
}

// Removes a file or folder with everything in it. A link is removed itself and never followed. False when
// something stayed.
bool removeEntry(const fs::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    const bool link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (!link && (attributes & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(path.c_str()) != FALSE;
    bool all = true;
    if (!link) {
        for (const fs::path& entry : entriesOf(path)) all = removeEntry(entry) && all;
    }
    return RemoveDirectoryW(path.c_str()) != FALSE && all;
}

// Says whether a file name ends with a given ending, ignoring case.
bool endsWith(const fs::path& name, std::wstring_view ending)
{
    std::wstring text = name.filename().wstring();
    if (text.size() < ending.size()) return false;
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
}

// Removes the logs, the lock files and every link in the kept folder, and keeps the rest.
bool cleanKeptFolder(const fs::path& folder)
{
    bool all = true;
    for (const fs::path& entry : entriesOf(folder)) {
        const DWORD attributes = GetFileAttributesW(entry.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) continue;
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) all = removeEntry(entry) && all;
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY) all = cleanKeptFolder(entry) && all;
        else if (endsWith(entry, L".log") || endsWith(entry, L".lock")) all = removeEntry(entry) && all;
    }
    return all;
}

// Empties the fetch folder except the kept folder, where only the logs go. False when something stayed.
bool emptyFetchFolder(const fs::path& folder)
{
    const fs::path keptName = pathFromUtf8(kFetchKeptFolder);
    bool all = true;
    for (const fs::path& entry : entriesOf(folder)) {
        const DWORD attributes = GetFileAttributesW(entry.c_str());
        const bool realFolder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
                                !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        const bool kept = realFolder && entry.filename() == keptName;
        all = (kept ? cleanKeptFolder(entry) : removeEntry(entry)) && all;
    }
    return all;
}

// Makes the fetch folder, or empties the one that is there and checks it is still Plynth's own. Never
// relabels a folder that still holds files.
Status prepareFetchFolder(const fs::path& folder)
{
    const fs::path inside = longPath(folder);
    const DWORD attributes = GetFileAttributesW(inside.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!makeLowIntegrityFolder(folder).ok()) return fail(ErrorCode::CannotWrite, "Plynth's fetch folder could not be made.");
        return {};
    }
    // The folder itself is checked first, so a link in its place is never emptied through.
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return fail(ErrorCode::CannotWrite, kFolderNotSetUp);
    }
    emptyFetchFolder(inside);
    if (entriesOf(inside).empty()) {
        if (!makeLowIntegrityFolder(folder).ok()) return fail(ErrorCode::CannotWrite, kFolderNotSetUp);
        return {};
    }
    return checkLowIntegrityFolder(folder);
}

// Copies one file into a new file. The copy takes the labels of the folder it lands in.
bool copyFileBytes(const fs::path& from, const fs::path& to)
{
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    std::vector<char> buffer(1 << 20);
    while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0) {
        out.write(buffer.data(), in.gcount());
        if (!out) return false;
    }
    return !in.bad() && static_cast<bool>(out.flush());
}

// Copies a folder's files and folders into a new folder and skips links. The copies are new files, so the
// library never holds a file a low integrity program may still change.
bool copyTree(const fs::path& from, const fs::path& to)
{
    if (!CreateDirectoryW(to.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    for (const fs::path& entry : entriesOf(from)) {
        const DWORD attributes = GetFileAttributesW(entry.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const fs::path target = to / entry.filename();
        const bool ok = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? copyTree(entry, target) : copyFileBytes(entry, target);
        if (!ok) return false;
    }
    return true;
}

// The same status with a fixed sentence when its message names the fetch folder, which is never shown.
Status withoutFolder(Status status, const fs::path& folder)
{
    if (!status.ok() && status.message.find(pathToUtf8(folder)) != std::string::npos) {
        status.message = "Plynth's fetch folder could not be written.";
    }
    return status;
}

// Holds the lock that lets one store fetch run at a time for this Windows user, across processes and
// threads.
class FetchLock {
public:
    FetchLock() = default;
    FetchLock(const FetchLock&) = delete;
    FetchLock& operator=(const FetchLock&) = delete;
    ~FetchLock()
    {
        if (held_) ReleaseMutex(mutex_);
        if (mutex_) CloseHandle(mutex_);
    }

    // Waits until the lock is free and takes it. Cancelled when stop is asked for first. A lock left by a
    // process that died counts as free.
    Status take(std::stop_token stop)
    {
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\PlynthStoreFetch");
        if (!mutex_) return fail(ErrorCode::CannotWrite, "The lock for store fetches could not be made.");
        for (;;) {
            if (stop.stop_requested()) return fail(ErrorCode::Cancelled, "The fetch was cancelled.");
            const DWORD waited = WaitForSingleObject(mutex_, 50);
            if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) {
                held_ = true;
                return {};
            }
            if (waited != WAIT_TIMEOUT) return fail(ErrorCode::CannotWrite, "The lock for store fetches could not be taken.");
        }
    }

private:
    HANDLE mutex_ = nullptr;
    bool held_ = false;
};

// Says whether a path is a real folder and not a link.
bool plainFolder(const fs::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

// The owner of a file or folder itself, never of where a link points. Empty when it cannot be read.
std::vector<unsigned char> ownerOf(const fs::path& path)
{
    const HANDLE handle = CreateFileW(path.c_str(), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::vector<unsigned char> out;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor) ==
            ERROR_SUCCESS &&
        owner && IsValidSid(owner)) {
        const auto* bytes = static_cast<const unsigned char*>(owner);
        out.assign(bytes, bytes + GetLengthSid(owner));
    }
    if (descriptor) LocalFree(descriptor);
    CloseHandle(handle);
    return out;
}

// Says whether two SIDs are the same.
bool sameSid(std::vector<unsigned char> a, std::vector<unsigned char> b)
{
    return !a.empty() && !b.empty() && EqualSid(a.data(), b.data());
}

// Says whether PlynthFetch is owned by this user, the administrators or the system, so no other user can
// take away the folder made in it.
bool trustedOwner(const fs::path& folder, const std::vector<unsigned char>& user)
{
    std::vector<unsigned char> owner = ownerOf(folder);
    if (owner.empty()) return false;
    return sameSid(owner, user) || IsWellKnownSid(owner.data(), WinBuiltinAdministratorsSid) ||
           IsWellKnownSid(owner.data(), WinLocalSystemSid);
}

}

namespace detail {

// Makes the last resort fetch folder fresh, for this user only and labelled low integrity.
Status makeShortFetchFolder(const fs::path& folder)
{
    const Status refuse = fail(ErrorCode::CannotWrite, noShortFetchFolderSentence());
    std::vector<unsigned char> user = currentUserSid();
    const fs::path parent = folder.parent_path();
    if (user.empty() || folder.empty() || parent == folder) return refuse;
    if (!CreateDirectoryW(parent.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return refuse;
    if (!plainFolder(parent) || !trustedOwner(parent, user)) return refuse;

    // A folder left by an earlier fetch that ended badly is removed. Anything else in its place is not touched.
    if (GetFileAttributesW(folder.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!plainFolder(folder) || !sameSid(ownerOf(folder), user) || !removeEntry(longPath(folder))) return refuse;
    }

    const PSID sid = user.data();
    std::vector<DWORD> aclBuffer((sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sid)) / sizeof(DWORD) + 1);
    auto* acl = reinterpret_cast<PACL>(aclBuffer.data());
    SECURITY_DESCRIPTOR descriptor{};
    const bool built = InitializeAcl(acl, static_cast<DWORD>(aclBuffer.size() * sizeof(DWORD)), ACL_REVISION) &&
                       AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE, FILE_ALL_ACCESS, sid) &&
                       InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
                       SetSecurityDescriptorOwner(&descriptor, sid, FALSE) &&
                       SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) &&
                       SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    if (!built) return refuse;
    // Made in one step with only this user's access, so it never exists open to others. It fails when a
    // folder appeared there after the check above.
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
    if (!CreateDirectoryW(folder.c_str(), &attributes)) return refuse;
    if (!makeLowIntegrityFolder(folder).ok()) {
        RemoveDirectoryW(folder.c_str());
        return refuse;
    }
    return {};
}

// Removes the last resort fetch folder, and PlynthFetch when nothing else is in it.
bool removeShortFetchFolder(const fs::path& folder)
{
    const bool gone = removeEntry(longPath(folder));
    // Only an empty folder is removed, so other users' folders in it stay.
    RemoveDirectoryW(longPath(folder.parent_path()).c_str());
    return gone;
}

}

// Reads and checks a fetch result.
FetchOutput readFetchResult(const Json& result, std::string_view task, std::string_view jobId)
{
    FetchOutput out;
    out.header = readBlenderResultHeader(result, task, jobId);
    if (!out.header.status.ok()) return out;
    const auto bad = [&](const std::string& where, const std::string& what) {
        out.header.status = fail(ErrorCode::BadFile, what, where);
        return out;
    };
    if (!result.contains("output") || !result["output"].is_object()) return bad("/output", "The fetch result has no output.");
    const Json& output = result["output"];
    if (!textField(output, "file", out.file, false) || !staysInside(out.file)) {
        return bad("/output/file", "The fetched file name is missing or leaves the output folder.");
    }
    if (!textField(output, "format", out.format, false) || out.format.empty()) return bad("/output/format", "Must be text.");
    if (!textField(output, "kind", out.kind, false) || (out.kind != kFetchKindModel && out.kind != kFetchKindMaterial)) {
        return bad("/output/kind", "Must be model or material.");
    }
    const std::pair<const char*, std::string*> optional[] = {
        {"name", &out.name}, {"author", &out.author}, {"license", &out.license}, {"reference", &out.reference}, {"material", &out.material}};
    for (const auto& [name, target] : optional) {
        if (!textField(output, name, *target, true)) return bad(std::string("/output/") + name, "Must be text or null.");
    }
    return out;
}

// Runs a fetch job in the folder chooseFetchFolder picks, one at a time, and moves what it brought into the
// work folder.
FetchRun runImportFetch(const ImportFetch& fetch, const fs::path& blender, const BlenderRunOptions& base,
                        const fs::path& workFolder, const std::function<void(const BlenderProgress&)>& onProgress,
                        std::stop_token stop)
{
    return detail::runImportFetchIn(chooseFetchFolder(), fetch, blender, base, workFolder, onProgress, stop);
}

// Runs a fetch job in a fetch folder that was already chosen.
FetchRun detail::runImportFetchIn(const FetchFolderChoice& choice, const ImportFetch& fetch, const fs::path& blender,
                                  const BlenderRunOptions& base, const fs::path& workFolder,
                                  const std::function<void(const BlenderProgress&)>& onProgress, std::stop_token stop)
{
    FetchRun run;
    std::error_code ec;
    if (!choice.status.ok()) {
        run.status = choice.status;
        return run;
    }
    const fs::path folder = choice.folder;
    // The store's helper fails on long paths, so a long folder stops here, before anything starts.
    if (folder.empty() || pathToUtf8(folder).size() > kMaxFetchFolderBytes) {
        run.status = fail(ErrorCode::CannotWrite, noShortFetchFolderSentence());
        return run;
    }
    const bool lastResort = choice.used == 4;
    fs::path work = fs::absolute(workFolder, ec).lexically_normal();
    work.make_preferred();
    fs::create_directories(work, ec);
    if (ec || !fs::is_directory(work, ec)) {
        run.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(work) + " could not be created.");
        return run;
    }

    FetchLock lock;
    if (Status s = lock.take(stop); !s.ok()) {
        run.status = s;
        return run;
    }
    if (Status s = lastResort ? makeShortFetchFolder(folder) : prepareFetchFolder(folder); !s.ok()) {
        run.status = s;
        return run;
    }
    const fs::path inside = longPath(folder);
    // The last resort folder sits outside the user's own folders, so it is removed after every fetch.
    const auto release = [&] {
        if (lastResort) removeShortFetchFolder(folder);
        else emptyFetchFolder(inside);
    };
    const fs::path output = folder / pathFromUtf8(kFetchOutputFolder);
    if (!CreateDirectoryW(longPath(output).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        release();
        run.status = fail(ErrorCode::CannotWrite, "Plynth's fetch folder could not be written.");
        return run;
    }

    Json params = fetch.params.is_object() ? fetch.params : Json::object();
    params["output_folder"] = pathToUtf8(output);
    params["private_folder"] = pathToUtf8(folder);
    const std::string jobId = newJobId(fetch.task);
    const Json job = makeBlenderJob(fetch.task, jobId, folder / pathFromUtf8(kResultFileName), std::move(params));

    BlenderRunOptions options = base;
    options.factoryStartup = false;
    options.timeout = fetch.timeout;
    options.steps = fetch.steps;
    options.privateFolder = folder;
    const BlenderJobRun blenderRun = runBlenderJob(blender, job, folder, options, onProgress, stop);
    // The job and the log stay with the import, as for its cleanup job. The log holds only the rebuilt step
    // lines and the job only what Plynth wrote, so neither holds what Blender or an add-on printed.
    for (const std::string_view name : {kJobFileName, kBlenderLogFileName}) {
        copyFileBytes(longPath(folder / pathFromUtf8(name)), longPath(work / pathFromUtf8(name)));
    }

    const auto finish = [&](Status status) {
        release();
        run.status = withoutFolder(std::move(status), folder);
        return run;
    };
    if (!blenderRun.status.ok()) {
        run.errorCode = blenderRun.errorCode;
        return finish(blenderRun.status);
    }
    run.output = readFetchResult(blenderRun.result, fetch.task, jobId);
    if (!run.output.header.status.ok()) {
        run.errorCode = run.output.header.errorCode;
        return finish(fail(ErrorCode::BlenderFailed, run.output.header.status.message));
    }
    if (!fs::is_regular_file(longPath(output / pathFromUtf8(run.output.file)), ec)) {
        return finish(fail(ErrorCode::BlenderFailed, "The fetch reported the file " + run.output.file + " but it is not there."));
    }

    const fs::path moved = work / pathFromUtf8(kFetchOutputFolder);
    if (!removeEntry(longPath(moved)) || !copyTree(longPath(output), longPath(moved))) {
        removeEntry(longPath(moved));
        return finish(fail(ErrorCode::CannotWrite, "The fetched files could not be copied to " + pathToUtf8(moved) + "."));
    }
    run.file = workFolder / pathFromUtf8(kFetchOutputFolder) / pathFromUtf8(run.output.file);
    if (!fs::is_regular_file(run.file, ec)) {
        return finish(fail(ErrorCode::BlenderFailed, "The fetch reported the file " + run.output.file + " but it is not there."));
    }
    return finish({});
}

// Puts a fetched model into an asset plan.
Status applyFetch(const FetchRun& run, const ImportFetch& fetch, ImportPlan& plan)
{
    if (run.output.kind != kFetchKindModel) return fail(ErrorCode::InvalidValue, wrongKindSentence(fetch, run.output.kind));
    plan.file = run.file;
    plan.format = run.output.format;
    if (plan.defaultName.empty()) plan.defaultName = run.output.name;
    if (plan.license.empty()) plan.license = run.output.license;
    if (plan.author.empty()) plan.author = run.output.author;
    if (plan.reference.empty()) plan.reference = run.output.reference;
    return {};
}

// Puts a fetched material into a material plan.
Status applyFetch(const FetchRun& run, const ImportFetch& fetch, MaterialImportPlan& plan)
{
    if (run.output.kind != kFetchKindMaterial) return fail(ErrorCode::InvalidValue, wrongKindSentence(fetch, run.output.kind));
    plan.path = run.file;
    plan.workerSource = run.output.format;
    if (!run.output.material.empty()) plan.blendMaterial = run.output.material;
    if (plan.defaultName.empty()) plan.defaultName = run.output.name;
    if (plan.license.empty()) plan.license = run.output.license;
    if (plan.author.empty()) plan.author = run.output.author;
    if (plan.reference.empty()) plan.reference = run.output.reference;
    return {};
}

}
