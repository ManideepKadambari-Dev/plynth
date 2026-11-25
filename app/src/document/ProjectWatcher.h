#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include <filesystem>

namespace plynth::app {

// Watches a project folder for changes to session.lock, plan.json and project.json. It combines
// the folder watch with a 500 ms poll, because atomic renames drop plain file watches and expiry
// of a session changes no file at all. The same poll also looks at build/build.lock, so a build or an
// export started by another program is noticed.
class ProjectWatcher : public QObject {
    Q_OBJECT
public:
    explicit ProjectWatcher(QObject* parent = nullptr);

    // Starts watching a folder. An empty path stops watching.
    void setFolder(const std::filesystem::path& folder);
    // Turns the timers on or off. Off in screenshot mode, where nothing needs to be noticed later.
    void setActive(bool active);
    // While on, every poll asks for a check even when no file changed, so session expiry is noticed.
    void setAlwaysCheck(bool on);
    // Remembers the current state of plan.json and project.json as the app's own.
    void acknowledge();
    // Says whether plan.json or project.json differ from the last acknowledged state.
    bool outsideChange() const;

signals:
    // Sent once, 150 ms after the last event.
    void checkRequested();
    // Sent at the poll that sees build/build.lock appear, go or be written again.
    void jobLockChanged();

private:
    struct FileSig {
        bool exists = false;
        qint64 size = 0;
        qint64 modified = 0;
        bool operator==(const FileSig&) const = default;
    };
    struct Sigs {
        FileSig lock;
        FileSig plan;
        FileSig project;
        bool operator==(const Sigs&) const = default;
    };

    Sigs readSigs() const;
    FileSig readJobLock() const;
    void poll();

    std::filesystem::path folder_;
    QFileSystemWatcher watcher_;
    QTimer pollTimer_;
    QTimer debounce_;
    Sigs lastSeen_;
    Sigs own_;
    FileSig jobLock_;
    bool active_ = true;
    bool alwaysCheck_ = false;
};

}
