#pragma once

#include "Http.h"

#include "plynth/core/Update.h"

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

// The parts of the updater that the tests reach. Nothing here is read from a file, a variable or a setting, so
// a shipped program always uses the built in keys, addresses and checks. Tests swap the environment with
// ScopedUpdateEnvironment.
namespace plynth::update_detail {

// A release version: major.minor.patch.
struct Version {
    unsigned major = 0, minor = 0, patch = 0;
    auto operator<=>(const Version&) const = default;
};

// Reads "1.2.3": three whole numbers of at most 9 digits each, with no leading zeros and nothing else.
std::optional<Version> parseVersion(std::string_view text);

// Reads a release tag "v1.2.3" in the same strict form.
std::optional<Version> parseTag(std::string_view tag);

// Writes a version as "1.2.3".
std::string versionText(const Version& version);

// The trusted comment that the owner signs for a version.
std::string trustedCommentFor(const Version& version);

// Reads a trusted comment. Empty unless it is exactly trustedCommentFor of a valid version.
std::optional<Version> parseTrustedComment(std::string_view comment);

// A minisign public key.
struct PublicKey {
    std::array<unsigned char, 8> keyId{};
    std::array<unsigned char, 32> key{};
};

// The key id as minisign prints it: 16 upper case hex digits.
std::string keyIdText(const std::array<unsigned char, 8>& keyId);

// Reads one base64 public key line, as in the second line of a minisign .pub file.
std::optional<PublicKey> parsePublicKey(std::string_view line);

// The most public keys a program trusts at once.
inline constexpr std::size_t kMaxTrustedKeys = 3;

// Reads the trusted keys file: one key line each, empty lines and lines that start with "#" or
// "untrusted comment:" skipped. Lines that are not keys are skipped too. At most kMaxTrustedKeys are kept.
std::vector<PublicKey> parseTrustedKeys(std::string_view text);

// The keys compiled into this program from trusted-keys.txt.
const std::vector<PublicKey>& builtInKeys();

// The largest .minisig file read.
inline constexpr std::size_t kMaxSignatureBytes = 8 * 1024;

// The four lines of a .minisig file.
struct Signature {
    std::array<unsigned char, 8> keyId{};
    std::array<unsigned char, 64> fileSignature{};
    std::string trustedComment;
    std::array<unsigned char, 64> commentSignature{};
};

// Reads a .minisig file. Only the prehashed "ED" form is taken. Empty with a reason when it is not exactly
// four lines in the minisign form.
std::optional<Signature> parseSignature(std::string_view text, std::string& why);

// A signature whose trusted comment verified with one of the keys, and the version it names.
struct CheckedSignature {
    Signature signature;
    PublicKey key;
    Version version;
};

// Checks a .minisig text against the keys: the form, the key id, the signature over the trusted comment and
// the comment's exact text. SignatureInvalid with a plain reason when any of them fails. The file itself is
// checked later with fileMatches.
Status checkSignature(std::string_view text, const std::vector<PublicKey>& keys, CheckedSignature& out);

// BLAKE2b-512 of a whole file, the hash minisign signs.
using FileHash = std::array<unsigned char, 64>;

// Says whether the file signature of a checked signature fits a file's hash.
bool fileMatches(const CheckedSignature& checked, const FileHash& hash);

// Hashes bytes in memory, for tests and small files.
FileHash hashBytes(std::string_view bytes);

// Reads the file version (first three numbers) of a Windows program from its bytes. Empty when it has none.
std::optional<Version> programVersion(std::string_view bytes);

// What a program needs to tell an installed copy from any other.
struct InstallationProbe {
    std::filesystem::path programFolder;   // the folder of the running program
    std::string runningVersion;            // version() of the running program
    std::filesystem::path fixedFolder;     // %LOCALAPPDATA%\Programs\Plynth, the folder the installer uses
    std::optional<std::wstring> installLocation; // InstallLocation of the installer's uninstall entry
};

// The AppId of the installer and the registry key of its uninstall entry under HKEY_CURRENT_USER.
inline constexpr wchar_t kUninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{8473EE0D-F68A-4C90-BB1C-1D2F3A293B12}_is1";

// Decides whether a probe describes an installed release.
Installation detectInstallation(const InstallationProbe& probe);

// The probe of the running program.
InstallationProbe currentProbe();

// Says whether a package.json text is that of a release of the given version: the right format, platform and
// version, and not a trial.
bool isReleasePackage(std::string_view text, std::string_view version);

// Two folders are the same: absolute, tidy, no trailing separator, ignoring case.
bool sameFolder(const std::filesystem::path& a, const std::filesystem::path& b);

// What update-state.json holds.
struct UpdateLast {
    std::string from, to, started;
    std::filesystem::path log;
};
struct UpdateStateFile {
    std::optional<Version> highestSigned;
    std::optional<UpdateLast> last;
};

// Reads the state file. A missing file is an empty state. BadFile when it breaks schema/update-state.schema.json.
Status readUpdateState(const std::filesystem::path& file, UpdateStateFile& out);

// Writes the state file through a temporary file and a rename.
Status writeUpdateState(const std::filesystem::path& file, const UpdateStateFile& state);

// Raises highest_signed to a version when it is higher. A state that cannot be read is not overwritten.
Status rememberSigned(const std::filesystem::path& file, const Version& version);

// How the installer is started. Tests read the spec instead of starting a real installer when they replace
// startInstaller.
struct InstallerStart {
    std::filesystem::path program;
    std::wstring commandLine;
    std::filesystem::path workingFolder;
    void* inheritedHandle = nullptr;
};

// The installer's arguments in update mode, quoted as Windows expects.
std::wstring installerCommandLine(const std::filesystem::path& program, const std::filesystem::path& log, bool reopen);

// How the current process sits in a job object.
struct JobPlacement {
    bool inJob = false;
    bool killOnClose = false;
    bool breakawayAllowed = false; // JOB_OBJECT_LIMIT_BREAKAWAY_OK or JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK
};

// The job of the current process.
JobPlacement currentJobPlacement();

// Starts the installer outside the job of this process where allowed, with only the installer's own file
// handle inherited and no standard handles. UpdateBlocked when the job would end it and does not let it leave.
Status startInstallerProcess(const InstallerStart& start);

// A running process, for the blockers.
struct RunningProgram {
    unsigned long pid = 0;
    unsigned long parentPid = 0;
    std::filesystem::path image; // full path; empty when it cannot be read
};

// The programs running now.
std::vector<RunningProgram> runningPrograms();

// The blockers from given facts, so each can be tested alone.
struct BlockerFacts {
    Installation installation;
    std::vector<RunningProgram> programs;
    unsigned long selfPid = 0;
    std::vector<std::filesystem::path> projects; // the current project first, then the recent ones
    JobPlacement job;
};
std::vector<UpdateBlocker> blockersFrom(const BlockerFacts& facts);

// Everything the updater reads from the world. The default is the shipped program's.
struct UpdateEnvironment {
    std::string apiBase = "https://api.github.com";
    std::string downloadBase = "https://github.com";
    std::vector<std::string> allowedHosts = defaultAllowedHosts();
    bool allowPlainHttp = false;          // tests on 127.0.0.1 only
    bool useSystemProxy = true;
    std::chrono::milliseconds stallLimit{60000};
    long long maxInstallerBytes = 1024LL * 1024 * 1024;
    std::vector<PublicKey> keys = builtInKeys();
    std::function<Installation()> installation = [] { return currentInstallation(); };
    std::filesystem::path stateFile;      // empty: settingsFolder()/update-state.json
    std::filesystem::path downloadRoot;   // empty: the updates folder below LOCALAPPDATA or PLYNTH_CONFIG_DIR
    std::function<std::optional<long long>(const std::filesystem::path&)> freeSpace; // empty: the real one
    std::function<std::vector<UpdateBlocker>(const Installation&, const std::filesystem::path&)> blockers; // empty: real
    std::function<Status(const InstallerStart&)> startInstaller; // empty: startInstallerProcess
};

// The environment in use now.
const UpdateEnvironment& environment();

// Puts a test environment in place for its lifetime. Not thread safe: tests set it before any work starts.
class ScopedUpdateEnvironment {
public:
    explicit ScopedUpdateEnvironment(UpdateEnvironment environment);
    ~ScopedUpdateEnvironment();
    ScopedUpdateEnvironment(const ScopedUpdateEnvironment&) = delete;
    ScopedUpdateEnvironment& operator=(const ScopedUpdateEnvironment&) = delete;

private:
    const UpdateEnvironment* previous_;
    UpdateEnvironment own_;
};

// The state file in use.
std::filesystem::path stateFile();

// The folder that holds the download folders.
std::filesystem::path downloadRoot();

// The address of the release list, of a release page and of a release file, built from the environment.
std::string releasesListUrl();
std::string releasePageUrl(const Version& version);
std::string releaseFileUrl(const Version& version, std::string_view file);

// The signed file and its signature.
inline constexpr std::string_view kInstallerName = "PlynthSetup.exe";
inline constexpr std::string_view kSignatureName = "PlynthSetup.exe.minisig";

// The largest release list read, and how many releases of it are looked at.
inline constexpr long long kMaxReleaseListBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kMaxReleases = 30;

// The versions of a release list that are newer than current, highest first. Drafts and tags not in the form
// v<major>.<minor>.<patch> are skipped, and only the first kMaxReleases entries are read. Empty with a status
// when the text is not a list of releases.
Status newerReleases(std::string_view listText, const Version& current, std::vector<Version>& out);

// Removes old download folders below root, keeping keep. Only folders with a download folder's name are
// touched, only plain files inside them are removed, and nothing that is a link is followed.
void removeOldDownloads(const std::filesystem::path& root, const std::filesystem::path& keep);

// Says whether a name is that of a download folder: 32 lower case hex digits.
bool isDownloadFolderName(std::wstring_view name);

// Makes a VerifiedInstaller from an open file handle, for downloadUpdate and the tests.
struct InstallerAccess {
    static VerifiedInstaller make(void* handle, std::filesystem::path file, std::string version, long long size);
    static void* handle(const VerifiedInstaller& installer);
};

// Opens a file the way a verified installer is kept: read only, no write or delete sharing, no reparse point
// followed. Null when it cannot be opened or is not a plain file. forRename adds delete access, which the
// rename needs, but then every later open of the file must share delete, which the installer's own open of
// itself does not. So the kept handle is always opened without it.
void* openLocked(const std::filesystem::path& file, bool forRename = false);

// The mark of a packaged program, with the mutex names given so tests can use their own.
Status beginPackagedRunIn(const std::filesystem::path& programFolder, const wchar_t* runningMutex,
                          const wchar_t* setupMutex);

}
