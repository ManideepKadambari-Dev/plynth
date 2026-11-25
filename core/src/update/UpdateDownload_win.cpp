#include "UpdateInternals.h"

#include "plynth/core/ProjectFiles.h"

#include <sodium.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <vector>

namespace plynth {

// Takes over the file of another installer.
VerifiedInstaller::VerifiedInstaller(VerifiedInstaller&& other) noexcept
    : handle_(other.handle_), file_(std::move(other.file_)), version_(std::move(other.version_)), size_(other.size_)
{
    other.handle_ = nullptr;
}

// Takes over the file of another installer, closing this one's first.
VerifiedInstaller& VerifiedInstaller::operator=(VerifiedInstaller&& other) noexcept
{
    if (this != &other) {
        if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = other.handle_;
        file_ = std::move(other.file_);
        version_ = std::move(other.version_);
        size_ = other.size_;
        other.handle_ = nullptr;
    }
    return *this;
}

// Closes the file, which lets other programs change it again.
VerifiedInstaller::~VerifiedInstaller()
{
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
}

}

namespace plynth::update_detail {

namespace fs = std::filesystem;

namespace {

constexpr long long kSpaceMargin = 64LL * 1024 * 1024;

// Says whether a path is a link of any kind.
bool isLink(const fs::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

// A fresh download folder name: 32 random lower case hex digits.
std::wstring randomName()
{
    if (sodium_init() < 0) return {};
    unsigned char bytes[16];
    randombytes_buf(bytes, sizeof bytes);
    static const wchar_t digits[] = L"0123456789abcdef";
    std::wstring name;
    for (const unsigned char b : bytes) {
        name += digits[b >> 4];
        name += digits[b & 0x0f];
    }
    return name;
}

// The free bytes for the current user on the drive of a folder.
std::optional<long long> freeBytes(const fs::path& folder)
{
    const UpdateEnvironment& env = environment();
    if (env.freeSpace) return env.freeSpace(folder);
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(folder.c_str(), &available, nullptr, nullptr)) return std::nullopt;
    return static_cast<long long>(available.QuadPart);
}

// Marks an open file to be deleted when its handle closes, then closes it.
void deleteAndClose(HANDLE handle)
{
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof disposition);
    CloseHandle(handle);
}

// Renames an open file within its folder through its handle.
bool renameOpen(HANDLE handle, const fs::path& target)
{
    const std::wstring name = target.wstring();
    const std::size_t bytes = sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t);
    std::vector<unsigned char> buffer(bytes, 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->ReplaceIfExists = FALSE;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::copy(name.begin(), name.end(), info->FileName);
    return SetFileInformationByHandle(handle, FileRenameInfo, info, static_cast<DWORD>(bytes)) != 0;
}

// Removes a download folder that failed: its known files, then the folder if it is empty.
void removeFailed(const fs::path& folder)
{
    for (const wchar_t* name : {L"PlynthSetup.exe.part", L"PlynthSetup.exe"}) {
        const fs::path file = folder / name;
        if (!isLink(file)) DeleteFileW(file.c_str());
    }
    RemoveDirectoryW(folder.c_str());
}

// Reads the whole of an open file through a read only view, for the hash and the version.
struct FileView {
    HANDLE mapping = nullptr;
    const char* data = nullptr;
    std::size_t size = 0;

    FileView(HANDLE file, long long length)
    {
        if (length <= 0) return;
        mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping) data = static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (data) size = static_cast<std::size_t>(length);
    }
    ~FileView()
    {
        if (data) UnmapViewOfFile(data);
        if (mapping) CloseHandle(mapping);
    }
    FileView(const FileView&) = delete;
    FileView& operator=(const FileView&) = delete;
};

}

// Makes a VerifiedInstaller from an open handle.
VerifiedInstaller InstallerAccess::make(void* handle, fs::path file, std::string version, long long size)
{
    VerifiedInstaller installer;
    installer.handle_ = handle;
    installer.file_ = std::move(file);
    installer.version_ = std::move(version);
    installer.size_ = size;
    return installer;
}

// The open handle of a VerifiedInstaller.
void* InstallerAccess::handle(const VerifiedInstaller& installer)
{
    return installer.handle_;
}

// Opens a file read only with no write or delete sharing, refusing links and anything but a plain file.
void* openLocked(const fs::path& file)
{
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || info.nNumberOfLinks != 1) {
        CloseHandle(handle);
        return nullptr;
    }
    return handle;
}

// Says whether a name is that of a download folder.
bool isDownloadFolderName(std::wstring_view name)
{
    if (name.size() != 32) return false;
    for (const wchar_t c : name) {
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    }
    return true;
}

// Removes old download folders, touching only plain files inside folders with a download folder's name.
void removeOldDownloads(const fs::path& root, const fs::path& keep)
{
    if (root.empty() || isLink(root)) return;
    std::error_code ec;
    std::vector<fs::path> folders;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path folder = it->path();
        if (!isDownloadFolderName(folder.filename().wstring()) || isLink(folder)) continue;
        const DWORD attributes = GetFileAttributesW(folder.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!keep.empty() && sameFolder(folder, keep)) continue;
        folders.push_back(folder);
    }
    for (const fs::path& folder : folders) {
        std::vector<fs::path> files;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            const DWORD attributes = GetFileAttributesW(it->path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) continue;
            if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) continue;
            files.push_back(it->path());
        }
        ec.clear();
        for (const fs::path& file : files) DeleteFileW(file.c_str());
        RemoveDirectoryW(folder.c_str());
    }
}

}

namespace plynth {

// Downloads the installer of an offer into a fresh folder, then checks it through a handle no one can change.
UpdateDownload downloadUpdate(const UpdateOffer& offer, const std::function<void(long long, long long)>& progress,
                              std::stop_token stop)
{
    using namespace update_detail;
    const UpdateEnvironment& env = environment();
    UpdateDownload result;
    const Installation installation = env.installation();
    if (installation.kind != InstallKind::Installed) {
        result.status = fail(ErrorCode::UpdateNotInstalled, std::string(kNotInstalledMessage) + " " + std::string(kReleasesPage));
        return result;
    }
    if (env.keys.empty()) {
        result.status = fail(ErrorCode::UpdateNotAvailable, "This copy of Plynth has no update key built in, so it cannot update itself.");
        return result;
    }
    CheckedSignature checked;
    if (Status s = checkSignature(offer.signature, env.keys, checked); !s.ok()) {
        result.status = s;
        return result;
    }
    const auto offered = parseVersion(offer.version);
    if (!offered || checked.version != *offered) {
        result.status = fail(ErrorCode::SignatureInvalid, "The signature is not one for version " + offer.version + ".");
        return result;
    }
    const auto current = parseVersion(installation.version);
    if (!current || !(*offered > *current)) {
        result.status = fail(ErrorCode::UpdateNotAvailable, "Version " + offer.version + " is not newer than this copy.");
        return result;
    }
    UpdateStateFile state;
    if (Status s = readUpdateState(stateFile(), state); !s.ok()) {
        result.status = s;
        return result;
    }
    if (state.highestSigned && *offered < *state.highestSigned) {
        result.status = fail(ErrorCode::UpdateNotAvailable, "Version " + versionText(*state.highestSigned) +
                                                                " was seen before, so the older " + offer.version +
                                                                " is not installed.");
        return result;
    }

    const fs::path root = downloadRoot();
    std::error_code ec;
    if (!root.empty()) fs::create_directories(root, ec);
    if (root.empty() || ec || isLink(root)) {
        result.status = fail(ErrorCode::CannotWrite, "The updates folder " + pathToUtf8(root) + " cannot be used.");
        return result;
    }
    const std::wstring name = randomName();
    const fs::path folder = root / name;
    if (name.empty() || !CreateDirectoryW(folder.c_str(), nullptr)) {
        result.status = fail(ErrorCode::CannotWrite, "A download folder could not be made in " + pathToUtf8(root) + ".");
        return result;
    }
    const fs::path part = folder / L"PlynthSetup.exe.part";
    const fs::path installerFile = folder / L"PlynthSetup.exe";

    HANDLE out = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        removeFailed(folder);
        result.status = fail(ErrorCode::CannotWrite, "The installer could not be saved in " + pathToUtf8(folder) + ".");
        return result;
    }
    long long total = 0;
    long long done = 0;
    Status space;
    const auto onLength = [&](long long length) {
        total = length;
        if (length > 0) {
            const auto available = freeBytes(folder);
            if (available && *available < length + kSpaceMargin) {
                space = fail(ErrorCode::CannotWrite, "There is not enough free space on the drive of " + pathToUtf8(folder) +
                                                         " for the installer.");
            }
        }
        if (progress) progress(0, total);
    };
    const auto sink = [&](std::string_view piece) -> Status {
        if (!space.ok()) return space;
        DWORD written = 0;
        if (!WriteFile(out, piece.data(), static_cast<DWORD>(piece.size()), &written, nullptr) || written != piece.size()) {
            return fail(ErrorCode::CannotWrite, "The installer could not be saved. The drive may be full.");
        }
        done += static_cast<long long>(piece.size());
        if (progress) progress(done, total);
        return {};
    };
    const HttpResult got = httpGet({releaseFileUrl(*offered, kInstallerName), "application/octet-stream", env.maxInstallerBytes},
                                   sink, onLength, stop);
    const bool flushed = FlushFileBuffers(out) != 0;
    CloseHandle(out);
    if (!got.status.ok() || !flushed) {
        removeFailed(folder);
        result.status = got.status.ok() ? fail(ErrorCode::CannotWrite, "The installer could not be saved.") : got.status;
        return result;
    }

    void* locked = openLocked(part);
    if (!locked) {
        removeFailed(folder);
        result.status = fail(ErrorCode::SignatureInvalid, "The downloaded installer could not be opened for the check.");
        return result;
    }
    const HANDLE handle = static_cast<HANDLE>(locked);
    LARGE_INTEGER length{};
    GetFileSizeEx(handle, &length);
    Status verdict;
    {
        const FileView view(handle, length.QuadPart);
        if (!view.data) {
            verdict = fail(ErrorCode::SignatureInvalid, "The downloaded installer could not be read for the check.");
        } else {
            const std::string_view bytes(view.data, view.size);
            const auto ownVersion = programVersion(bytes);
            if (!fileMatches(checked, hashBytes(bytes))) {
                verdict = fail(ErrorCode::SignatureInvalid, "The downloaded installer does not match its signature, so it is not used.");
            } else if (!ownVersion || *ownVersion != *offered) {
                verdict = fail(ErrorCode::SignatureInvalid, "The downloaded installer is not version " + offer.version +
                                                                ", so it is not used.");
            }
        }
    }
    if (verdict.ok() && !renameOpen(handle, installerFile)) {
        verdict = fail(ErrorCode::CannotWrite, "The checked installer could not be named PlynthSetup.exe.");
    }
    if (!verdict.ok()) {
        deleteAndClose(handle);
        removeFailed(folder);
        result.status = verdict;
        return result;
    }
    result.installer = InstallerAccess::make(locked, installerFile, offer.version, length.QuadPart);
    return result;
}

}
