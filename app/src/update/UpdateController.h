#pragma once

#include <plynth/core/Update.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace plynth::app {

class LibraryHost;

// Runs the update check, the download and the start of the installer for the app, each slow step on a worker
// thread. The notice bar and the update dialog only read it and redraw when changed() is sent.
class UpdateController : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,         // nothing to show
        Checking,     // a check by hand is running
        UpToDate,     // a check by hand found nothing newer
        CheckFailed,  // a check by hand failed, message() says why
        Offer,        // a newer signed release is known
        Downloading,  // the installer is being downloaded and verified
        Verified,     // the installer is downloaded, verified and locked, waiting for Install now
        Refused,      // the app or another program is busy, message() says what to finish first
        Failed,       // the download or the start of the installer failed, message() says why
        Starting,     // the installer runs and Plynth closes
        NotInstalled, // this copy is not an installed release and never updates
    };

    // What only the main window can answer or do.
    struct Hooks {
        std::function<QString()> busyReason;                 // empty when no build, export, import or detection runs
        std::function<std::filesystem::path()> projectFolder; // the open project, empty when none
        std::function<bool()> askToClose;                    // the normal close questions; false means cancel
        std::function<void()> closeForInstaller;             // closes the app without asking again
    };

    explicit UpdateController(LibraryHost& host, QObject* parent = nullptr);
    ~UpdateController() override;

    void setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

    bool installed() const;
    State state() const { return state_; }
    QString currentVersion() const;
    // The release on offer, empty when none is known.
    const std::optional<plynth::UpdateOffer>& offer() const { return offer_; }
    QString offerVersion() const;
    QString message() const { return message_; }
    QStringList warnings() const { return warnings_; }
    long long downloaded() const { return done_; }
    // True while a verified installer is held for Install now.
    bool hasInstaller() const { return installer_.has_value() || (canned_ && total_ > 0 && done_ == total_); }
    // The size of the installer, 0 until the download knows it.
    long long downloadSize() const { return total_; }
    // True while the notice under the title bar should show: a check at start found an offer and Later was not pressed.
    bool noticeShown() const { return noticeShown_; }

    // The switch "Check for updates at start", saved in the settings.
    bool checkAtStart() const;
    plynth::Status setCheckAtStart(bool on);

    // The check once per start. It asks shouldCheckAtStart first and stays quiet when it fails.
    void checkAtStartOfRun();
    // A check by hand. It says what it found, or why it failed.
    void checkByHand();
    // Downloads and verifies the offer, checking first when none is known yet.
    void update();
    // Stops a check or a download. A verified installer is let go, so nothing is installed.
    void cancel();
    // Starts the verified installer after the busy checks and the close questions, then closes the app.
    void install();
    // Later: hides the notice until the next start.
    void dismissNotice();

    // For checks only: shows a canned state with a made up offer. Nothing is sent, downloaded or started.
    bool showCanned(const QString& name);
    static QStringList cannedNames();
    bool canned() const { return canned_; }

signals:
    void changed();

private:
    void runCheck(bool byHand, bool thenDownload);
    void checkDone(int generation, const plynth::UpdateCheck& check);
    void startDownload();
    void downloadProgress(int generation, long long done, long long total);
    void downloadDone(int generation, std::shared_ptr<plynth::UpdateDownload> download);
    QString refusal() const;
    void setState(State state, const QString& message = QString());

    LibraryHost& host_;
    Hooks hooks_;
    plynth::Installation installation_;
    State state_ = State::Idle;
    std::optional<plynth::UpdateOffer> offer_;
    std::optional<plynth::VerifiedInstaller> installer_;
    QString message_;
    QStringList warnings_;
    QString cannedCurrent_;
    long long done_ = 0;
    long long total_ = 0;
    bool noticeShown_ = false;
    bool byHand_ = false;
    bool thenDownload_ = false;
    bool checking_ = false;
    bool canned_ = false;
    int generation_ = 0;
    std::jthread worker_;
};

}
