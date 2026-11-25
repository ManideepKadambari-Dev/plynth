#include "OutsideJobWatch.h"

#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/JobProgress.h>

#include <system_error>

namespace plynth::app {

namespace {

constexpr int kPollMs = 300;

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// Sets up the poll, which runs only while an outside job is followed.
OutsideJobWatch::OutsideJobWatch(QObject* parent) : QObject(parent)
{
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, &OutsideJobWatch::poll);
}

// Forgets any job being followed and looks at the new folder's lock once.
void OutsideJobWatch::setFolder(const std::filesystem::path& folder)
{
    if (folder == folder_) {
        recheck();
        return;
    }
    const bool wasActive = active_;
    timer_.stop();
    active_ = false;
    kind_.clear();
    jobId_.clear();
    message_.clear();
    percent_ = -1;
    folder_ = folder;
    recheck();
    if (wasActive && !active_)
        emit changed();
}

// Keeps the question the watch asks before it follows a lock.
void OutsideJobWatch::setOwnJobCheck(std::function<bool()> ownJob)
{
    ownJob_ = std::move(ownJob);
}

// Starts following when a live lock belongs to another program, or polls at once while following.
void OutsideJobWatch::recheck()
{
    if (active_) {
        poll();
        return;
    }
    if (folder_.empty() || (ownJob_ && ownJob_()))
        return;
    std::error_code ec;
    if (!std::filesystem::exists(plynth::buildFolder(folder_) / std::string(plynth::kBuildLockFileName), ec))
        return;
    if (!plynth::jobRunning(folder_))
        return;
    if (const std::optional<plynth::JobProgress> p = plynth::readJobProgress(folder_); p && p->owner == plynth::currentProcess())
        return;
    active_ = true;
    kind_.clear();
    jobId_.clear();
    message_.clear();
    percent_ = -1;
    timer_.start();
    poll();
    if (active_)
        emit changed();
}

// Writes the cancel marker that the outside job watches.
bool OutsideJobWatch::cancel()
{
    return active_ && plynth::requestCancel(folder_);
}

// Reads the progress, or ends when the lock is gone or no longer live.
void OutsideJobWatch::poll()
{
    if (!active_)
        return;
    if (!plynth::jobRunning(folder_)) {
        timer_.stop();
        active_ = false;
        emit ended(kind_, jobId_);
        return;
    }
    const std::optional<plynth::JobProgress> p = plynth::readJobProgress(folder_);
    if (!p)
        return;
    // The lock turned out to be this app's own job, seen before its first progress was written.
    if (p->owner == plynth::currentProcess()) {
        timer_.stop();
        active_ = false;
        emit changed();
        return;
    }
    const QString kind = fromCore(p->kind);
    const QString jobId = fromCore(p->jobId);
    const QString message = fromCore(p->message);
    if (kind == kind_ && jobId == jobId_ && message == message_ && p->percent == percent_)
        return;
    kind_ = kind;
    jobId_ = jobId;
    message_ = message;
    percent_ = p->percent;
    emit changed();
}

}
