#include "UpdateController.h"

#include "catalog/LibraryHost.h"

#include <plynth/core/Settings.h>
#include <plynth/core/Version.h>

#include <QMetaObject>
#include <QPointer>

#include <string_view>
#include <utility>

namespace plynth::app {

namespace {

// Turns a core sentence into Qt text.
QString fromCore(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// The made up offer the canned states show.
plynth::UpdateOffer cannedOffer()
{
    plynth::UpdateOffer offer;
    offer.version = "0.2.0";
    offer.releaseUrl = "https://github.com/ManideepKadambari-Dev/plynth/releases/tag/v0.2.0";
    offer.earlyPreview = true;
    return offer;
}

}

// Finds out once whether this copy is an installed release. Nothing is sent.
UpdateController::UpdateController(LibraryHost& host, QObject* parent)
    : QObject(parent), host_(host), installation_(plynth::currentInstallation())
{
}

// Stops a running check or download and waits for the worker before the controller goes away.
UpdateController::~UpdateController()
{
    worker_.request_stop();
    worker_ = std::jthread();
}

// True for an installed release, or for the canned states that act as one.
bool UpdateController::installed() const
{
    if (canned_)
        return state_ != State::NotInstalled;
    return installation_.kind == plynth::InstallKind::Installed;
}

// The version of this Plynth.
QString UpdateController::currentVersion() const
{
    return cannedCurrent_.isEmpty() ? fromCore(plynth::version()) : cannedCurrent_;
}

// The version on offer, empty when none.
QString UpdateController::offerVersion() const
{
    return offer_ ? fromCore(offer_->version) : QString();
}

// The switch as the settings have it.
bool UpdateController::checkAtStart() const
{
    return host_.settings().updateCheck;
}

// Saves the switch. A copy that is not installed keeps it too, for when it is installed.
plynth::Status UpdateController::setCheckAtStart(bool on)
{
    if (canned_)
        return {};
    const plynth::Status status = host_.apply([on](plynth::MachineSettings& s) { s.updateCheck = on; });
    emit changed();
    return status;
}

// The check at start: only when core says an installed release with readable settings has the switch on.
void UpdateController::checkAtStartOfRun()
{
    if (canned_ || checking_ || state_ != State::Idle)
        return;
    if (!plynth::shouldCheckAtStart(installation_, plynth::loadSettings()))
        return;
    runCheck(false, false);
}

// A check by hand. A copy that is not installed says so and sends nothing.
void UpdateController::checkByHand()
{
    if (canned_)
        return;
    if (!installed()) {
        setState(State::NotInstalled);
        return;
    }
    switch (state_) {
    case State::Downloading:
    case State::Verified:
    case State::Starting:
        return;
    default:
        break;
    }
    if (checking_) {
        byHand_ = true;
        setState(State::Checking);
        return;
    }
    runCheck(true, false);
}

// Update pressed: refused while the app is busy, otherwise the download starts, after a check when needed.
void UpdateController::update()
{
    if (canned_)
        return;
    if (!installed()) {
        setState(State::NotInstalled);
        return;
    }
    if (state_ == State::Downloading || state_ == State::Verified || state_ == State::Starting)
        return;
    const QString reason = refusal();
    if (!reason.isEmpty()) {
        setState(State::Refused, reason);
        return;
    }
    if (installer_) {
        setState(State::Verified);
        return;
    }
    if (checking_) {
        byHand_ = true;
        thenDownload_ = true;
        setState(State::Checking);
        return;
    }
    if (!offer_) {
        runCheck(true, true);
        return;
    }
    startDownload();
}

// Cancel: the worker is asked to stop and a verified installer is let go. The project is not touched.
void UpdateController::cancel()
{
    if (canned_)
        return;
    ++generation_;
    worker_.request_stop();
    checking_ = false;
    byHand_ = false;
    thenDownload_ = false;
    installer_.reset();
    done_ = 0;
    total_ = 0;
    setState(offer_ ? State::Offer : State::Idle);
}

// Install now: checks again that nothing is busy, asks the normal close questions once, starts the installer
// and only then closes the app.
void UpdateController::install()
{
    if (canned_ || !installer_ || state_ == State::Starting)
        return;
    const QString reason = refusal();
    if (!reason.isEmpty()) {
        setState(State::Refused, reason);
        return;
    }
    if (hooks_.askToClose && !hooks_.askToClose()) {
        setState(State::Verified, tr("Nothing was installed. Press Install now when you are ready."));
        return;
    }
    const std::filesystem::path folder = hooks_.projectFolder ? hooks_.projectFolder() : std::filesystem::path();
    plynth::VerifiedInstaller installer = std::move(*installer_);
    installer_.reset();
    const plynth::Status status = plynth::launchUpdate(std::move(installer), folder, true);
    if (!status.ok()) {
        setState(State::Failed, fromCore(status.message));
        return;
    }
    setState(State::Starting);
    if (hooks_.closeForInstaller)
        hooks_.closeForInstaller();
}

// Later: the notice goes until the next start. The offer stays for Help, Update.
void UpdateController::dismissNotice()
{
    noticeShown_ = false;
    emit changed();
}

// Starts a check on the worker thread. Only a check by hand shows its result when nothing newer is found.
void UpdateController::runCheck(bool byHand, bool thenDownload)
{
    const int generation = ++generation_;
    checking_ = true;
    byHand_ = byHand;
    thenDownload_ = thenDownload;
    warnings_.clear();
    if (byHand)
        setState(State::Checking);
    QPointer<UpdateController> self(this);
    worker_ = std::jthread([self, generation](std::stop_token stop) {
        plynth::UpdateCheck check = plynth::checkForUpdate(stop);
        if (stop.stop_requested())
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, check]() {
                if (self)
                    self->checkDone(generation, check);
            },
            Qt::QueuedConnection);
    });
}

// Takes the result of a check. A failed check at start stays quiet.
void UpdateController::checkDone(int generation, const plynth::UpdateCheck& check)
{
    if (generation != generation_)
        return;
    checking_ = false;
    warnings_.clear();
    for (const plynth::UpdateWarning& warning : check.warnings)
        warnings_ << fromCore(warning.message);
    if (!check.status.ok()) {
        if (byHand_)
            setState(State::CheckFailed, fromCore(check.status.message));
        else
            setState(State::Idle);
        return;
    }
    offer_ = check.offer;
    if (!offer_) {
        noticeShown_ = false;
        setState(byHand_ ? State::UpToDate : State::Idle);
        return;
    }
    if (!byHand_)
        noticeShown_ = true;
    if (thenDownload_) {
        thenDownload_ = false;
        const QString reason = refusal();
        if (!reason.isEmpty()) {
            setState(State::Refused, reason);
            return;
        }
        startDownload();
        return;
    }
    setState(State::Offer);
}

// Downloads and verifies the installer on the worker thread. Editing goes on meanwhile.
void UpdateController::startDownload()
{
    if (!offer_)
        return;
    const int generation = ++generation_;
    done_ = 0;
    total_ = 0;
    installer_.reset();
    setState(State::Downloading);
    const plynth::UpdateOffer offer = *offer_;
    QPointer<UpdateController> self(this);
    worker_ = std::jthread([self, generation, offer](std::stop_token stop) {
        long long lastStep = -1;
        auto download = std::make_shared<plynth::UpdateDownload>(plynth::downloadUpdate(
            offer,
            [&](long long done, long long total) {
                // About two hundred redraws for the whole download are enough.
                const long long step = total > 0 ? done * 200 / total : done / (1LL << 20);
                if (step == lastStep)
                    return;
                lastStep = step;
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, generation, done, total]() {
                        if (self)
                            self->downloadProgress(generation, done, total);
                    },
                    Qt::QueuedConnection);
            },
            stop));
        if (stop.stop_requested())
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, download]() {
                if (self)
                    self->downloadDone(generation, download);
            },
            Qt::QueuedConnection);
    });
}

// Shows how far the download has come.
void UpdateController::downloadProgress(int generation, long long done, long long total)
{
    if (generation != generation_ || state_ != State::Downloading)
        return;
    done_ = done;
    total_ = total;
    emit changed();
}

// Keeps the verified installer, or says why the download failed. Busy work is checked again here.
void UpdateController::downloadDone(int generation, std::shared_ptr<plynth::UpdateDownload> download)
{
    if (generation != generation_)
        return;
    if (!download->status.ok() || !download->installer) {
        setState(State::Failed, download->status.ok() ? tr("The download gave no installer.")
                                                      : fromCore(download->status.message));
        return;
    }
    installer_ = std::move(download->installer);
    if (total_ > 0)
        done_ = total_;
    const QString reason = refusal();
    if (!reason.isEmpty()) {
        setState(State::Refused, reason);
        return;
    }
    setState(State::Verified);
}

// What must finish or close first, or empty when the update may go on. Nothing is cancelled or closed.
QString UpdateController::refusal() const
{
    QStringList reasons;
    if (hooks_.busyReason) {
        const QString busy = hooks_.busyReason();
        if (!busy.isEmpty())
            reasons << busy;
    }
    const std::filesystem::path folder = hooks_.projectFolder ? hooks_.projectFolder() : std::filesystem::path();
    for (const plynth::UpdateBlocker& blocker : plynth::updateBlockers(installation_, folder))
        reasons << fromCore(blocker.message);
    return reasons.join(QLatin1Char(' '));
}

// Moves to a state and tells the views.
void UpdateController::setState(State state, const QString& message)
{
    state_ = state;
    message_ = message;
    emit changed();
}

// The names --update-state takes.
QStringList UpdateController::cannedNames()
{
    return {QStringLiteral("notice"),     QStringLiteral("checking"), QStringLiteral("up-to-date"),
            QStringLiteral("failed"),     QStringLiteral("offer"),    QStringLiteral("downloading"),
            QStringLiteral("ready"),      QStringLiteral("busy"),     QStringLiteral("not-installed"),
            QStringLiteral("long")};
}

// Sets up a canned state for a screenshot. From here on no action sends, downloads or starts anything.
bool UpdateController::showCanned(const QString& name)
{
    if (!cannedNames().contains(name))
        return false;
    ++generation_;
    worker_.request_stop();
    canned_ = true;
    checking_ = false;
    installer_.reset();
    cannedCurrent_ = QStringLiteral("0.1.0");
    offer_ = cannedOffer();
    warnings_.clear();
    noticeShown_ = false;
    done_ = 0;
    total_ = 0;
    const long long size = 48LL * 1024 * 1024 + 3LL * 1024 * 1024 / 10;
    if (name == QStringLiteral("notice")) {
        noticeShown_ = true;
        setState(State::Idle);
    } else if (name == QStringLiteral("checking")) {
        offer_.reset();
        setState(State::Checking);
    } else if (name == QStringLiteral("up-to-date")) {
        offer_.reset();
        cannedCurrent_ = QStringLiteral("0.2.0");
        setState(State::UpToDate);
    } else if (name == QStringLiteral("failed")) {
        offer_.reset();
        setState(State::CheckFailed, tr("GitHub could not be reached. Check the internet connection, then try again."));
    } else if (name == QStringLiteral("offer")) {
        setState(State::Offer);
    } else if (name == QStringLiteral("downloading")) {
        done_ = size * 2 / 5;
        total_ = size;
        setState(State::Downloading);
    } else if (name == QStringLiteral("ready")) {
        done_ = size;
        total_ = size;
        setState(State::Verified);
    } else if (name == QStringLiteral("busy")) {
        done_ = size;
        total_ = size;
        setState(State::Refused, tr("A build is running in this window. Wait for it to finish or cancel it, then "
                                    "press Install now. Nothing was stopped."));
    } else if (name == QStringLiteral("not-installed")) {
        offer_.reset();
        setState(State::NotInstalled);
    } else {
        offer_.reset();
        warnings_ << tr("The signature of Plynth 0.4.0 does not match the installer, so that release was skipped. "
                        "Download it by hand only if you can check it with the published public keys.")
                  << tr("The signature of Plynth 0.3.1 names another file, so that release was skipped.");
        setState(State::CheckFailed,
                 tr("No release newer than 0.1.0 has a valid signature. Plynth stays at 0.1.0. The releases that "
                    "were skipped are listed below, and the Releases page has the notes for each of them."));
    }
    return true;
}

}
