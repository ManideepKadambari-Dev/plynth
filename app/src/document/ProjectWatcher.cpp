#include "ProjectWatcher.h"

#include <QFileInfo>

#include <plynth/core/EditSession.h>
#include <plynth/core/Project.h>

namespace plynth::app {

namespace {

constexpr int kPollMs = 500;
constexpr int kDebounceMs = 150;

// Reads the size and write time of one file in the folder.
QFileInfo infoOf(const std::filesystem::path& folder, std::string_view name)
{
    return QFileInfo(QString::fromStdWString((folder / std::filesystem::path(std::string(name))).wstring()));
}

}

// Sets up the folder watch, the poll and the 150 ms fold of events into one check.
ProjectWatcher::ProjectWatcher(QObject* parent)
    : QObject(parent)
{
    pollTimer_.setInterval(kPollMs);
    debounce_.setInterval(kDebounceMs);
    debounce_.setSingleShot(true);
    connect(&pollTimer_, &QTimer::timeout, this, &ProjectWatcher::poll);
    connect(&debounce_, &QTimer::timeout, this, &ProjectWatcher::checkRequested);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this]() { debounce_.start(); });
    pollTimer_.start();
}

// Watches the folder itself, because a rename drops the watch on a single file.
void ProjectWatcher::setFolder(const std::filesystem::path& folder)
{
    if (!watcher_.directories().isEmpty())
        watcher_.removePaths(watcher_.directories());
    folder_ = folder;
    if (folder_.empty())
        return;
    watcher_.addPath(QString::fromStdWString(folder_.wstring()));
    lastSeen_ = readSigs();
}

// Starts or stops the poll and the folder events.
void ProjectWatcher::setActive(bool active)
{
    active_ = active;
    if (active)
        pollTimer_.start();
    else {
        pollTimer_.stop();
        debounce_.stop();
    }
}

// Makes every poll ask for a check.
void ProjectWatcher::setAlwaysCheck(bool on)
{
    alwaysCheck_ = on;
}

// Records plan.json and project.json as they are now, so the app's own writes are not outside changes.
void ProjectWatcher::acknowledge()
{
    own_ = readSigs();
}

// Compares the two project files with the last acknowledged state.
bool ProjectWatcher::outsideChange() const
{
    const Sigs now = readSigs();
    return !(now.plan == own_.plan) || !(now.project == own_.project);
}

// Reads the state of the three watched files.
ProjectWatcher::Sigs ProjectWatcher::readSigs() const
{
    Sigs sigs;
    if (folder_.empty())
        return sigs;
    auto read = [this](std::string_view name) {
        const QFileInfo info = infoOf(folder_, name);
        FileSig sig;
        sig.exists = info.exists();
        if (sig.exists) {
            sig.size = info.size();
            sig.modified = info.lastModified().toMSecsSinceEpoch();
        }
        return sig;
    };
    sigs.lock = read(plynth::kSessionFileName);
    sigs.plan = read(plynth::kPlanFileName);
    sigs.project = read(plynth::kProjectFileName);
    return sigs;
}

// Asks for a check when a watched file changed, or always while a session may expire.
void ProjectWatcher::poll()
{
    if (!active_ || folder_.empty())
        return;
    const Sigs now = readSigs();
    if (!(now == lastSeen_)) {
        lastSeen_ = now;
        debounce_.start();
    } else if (alwaysCheck_ && !debounce_.isActive()) {
        debounce_.start();
    }
}

}
