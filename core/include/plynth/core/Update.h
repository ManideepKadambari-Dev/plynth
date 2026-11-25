#pragma once

#include "Settings.h"
#include "Status.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Updating an installed Plynth from GitHub Releases. Only an installed release checks or updates. Every other
// copy makes no request and changes nothing. A release is offered only when its installer was signed with a
// key built into this program, its signed version is higher than this one, and it is not below the highest
// signed version seen before. Nothing updates without being asked: the caller asks, then this code downloads,
// verifies and starts the installer, and the caller exits so the installer can replace it.

// The Releases page, opened by hand from the messages below.
inline constexpr std::string_view kReleasesPage = "https://github.com/ManideepKadambari-Dev/plynth/releases";

// The sentence for a copy that is not installed, given with UpdateNotInstalled.
inline constexpr std::string_view kNotInstalledMessage =
    "This copy of Plynth was not installed, so it cannot update itself. Build it again from the source, or "
    "install Plynth with PlynthSetup.exe from the Releases page.";

enum class InstallKind { Installed, NotInstalled };

// The running copy of Plynth. folder is the folder of the running program, version its own version.
struct Installation {
    InstallKind kind = InstallKind::NotInstalled;
    std::filesystem::path folder;
    std::string version;
};

// Says whether this copy is an installed release: a release package.json beside the program, and the program
// folder is both the fixed install folder and the folder the installer recorded. Reads no network.
Installation currentInstallation();

// For a packaged program: marks it running. UpdateInProgress while setup runs.
// Called once at start by the app and the CLI. A program without a package.json beside it does nothing and
// gets ok. The mark lasts until the process ends.
Status beginPackagedRun();

// A release that may be installed. signature is the text of its PlynthSetup.exe.minisig, already verified.
// earlyPreview is true for a 0.x version.
struct UpdateOffer {
    std::string version, releaseUrl, signature;
    bool earlyPreview = false;
};

// Something worth telling the user in a check by hand, such as a release whose signature failed.
// Codes: "signature_invalid" (with the version in the message).
struct UpdateWarning {
    std::string code, message;
};

// The answer of a check. status is ok whether or not an offer was found. UpdateNotInstalled for a copy that is
// not installed (no request made), UpdateNotAvailable for an installed copy with no public key built in,
// NetworkFailed when the release list could not be read, Cancelled when stopped.
struct UpdateCheck {
    Status status;
    std::string current;
    std::optional<UpdateOffer> offer;
    std::vector<UpdateWarning> warnings;
};

// Asks GitHub for the list of releases and returns the newest one that may be installed. Blocks, so the app
// calls it from a worker thread.
UpdateCheck checkForUpdate(std::stop_token = {});

namespace update_detail {
struct InstallerAccess;
}

// A downloaded installer whose signature verified. It keeps the file open so no other program can change,
// replace or delete it, until it is given to launchUpdate or dropped. Dropping it closes the file and leaves
// it for the cleanup at a later start.
class VerifiedInstaller {
public:
    VerifiedInstaller(VerifiedInstaller&& other) noexcept;
    VerifiedInstaller& operator=(VerifiedInstaller&& other) noexcept;
    VerifiedInstaller(const VerifiedInstaller&) = delete;
    VerifiedInstaller& operator=(const VerifiedInstaller&) = delete;
    ~VerifiedInstaller();

    // The installer file.
    const std::filesystem::path& file() const { return file_; }
    // The signed version it installs.
    const std::string& version() const { return version_; }
    // Its size in bytes.
    long long size() const { return size_; }

private:
    friend struct update_detail::InstallerAccess;
    VerifiedInstaller() = default;

    void* handle_ = nullptr;
    std::filesystem::path file_;
    std::string version_;
    long long size_ = 0;
};

// The answer of a download. status is ok with an installer, or UpdateNotInstalled, UpdateNotAvailable,
// SignatureInvalid, NetworkFailed, CannotWrite (disk full or folder refused) or Cancelled, with no installer.
struct UpdateDownload {
    Status status;
    std::optional<VerifiedInstaller> installer;
};

// Downloads the installer of an offer, checking the offer again first. progress, when given, is called on the
// calling thread with the bytes so far and the total (0 when not known yet). Blocks.
UpdateDownload downloadUpdate(const UpdateOffer&, const std::function<void(long long done, long long total)>& = {},
                              std::stop_token = {}); // checks the offer again

// A reason the update cannot start now. Codes:
//   "plynth_running"  another Plynth window or command from the install folder runs (the message says what)
//   "edit_session"    an edit session is active in a recent project (the message names it)
//   "build_running"   a build or export holds the lock of a recent project
//   "parent_job"      the program that started Plynth would end the installer with it
//   "not_installed"   the copy is not installed
// The app adds its own reasons for work that runs inside it.
struct UpdateBlocker {
    std::string code, message;
};

// Every reason the update cannot start now. This process and the launcher that started it do not count.
// currentFolder is the CLI's current project or the app's open project, empty when there is none.
std::vector<UpdateBlocker> updateBlockers(const Installation&, const std::filesystem::path& currentFolder);

// Starts the verified installer in update mode, outside any job that would end it, and records the start in
// the update state. reopen asks the installer to open the app when it is done. UpdateBlocked when a blocker
// holds, UpdateNotInstalled when the copy is not installed. The caller then exits.
Status launchUpdate(VerifiedInstaller&&, const std::filesystem::path& currentFolder,
                    bool reopen); // checks blockers again. The caller then exits

// True only for an installed release, settings that could be read and the switch on.
bool shouldCheckAtStart(const Installation&, const SettingsLoad&);

enum class LastUpdateResult { None, Succeeded, NotFinished };

// What "plynth update --status" shows. last is Succeeded when the last started update reached the version it
// named, NotFinished when it did not, None when no update was started. from, to and log describe that start.
// keyIds are the public key ids built in, as 16 upper case hex digits.
struct UpdateStatus {
    Installation installation;
    bool checkAtStart = false;
    LastUpdateResult last = LastUpdateResult::None;
    std::string from, to;
    std::filesystem::path log;
    std::vector<std::string> keyIds;
};

// Reads the state of updating. Makes no request.
UpdateStatus updateStatus(); // no network

// The stable lower case name of a last update result: "none", "succeeded" or "not_finished".
std::string_view lastUpdateResultName(LastUpdateResult result);

}
