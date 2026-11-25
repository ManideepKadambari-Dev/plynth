#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <filesystem>
#include <functional>

namespace plynth::app {

// Follows a build or an export of the open project that another program runs, such as "plynth build" from a
// terminal. It reads build/progress.json about every 300 ms, and only while build/build.lock exists and the app
// runs no job of its own. It never touches the document.
class OutsideJobWatch : public QObject {
    Q_OBJECT
public:
    explicit OutsideJobWatch(QObject* parent = nullptr);

    // Follows the project in this folder. Another folder, or an empty path, stops following without sending ended.
    void setFolder(const std::filesystem::path& folder);
    // Tells the watch how to ask whether the app runs a build or an export itself.
    void setOwnJobCheck(std::function<bool()> ownJob);
    // Looks at the lock again, for example when the watcher saw it change or the app's own job ended.
    void recheck();
    // Asks the outside job to stop. False when no live lock says one runs.
    bool cancel();

    bool active() const { return active_; }
    // "build", "export", or empty until the job wrote its first progress.
    QString kind() const { return kind_; }
    QString message() const { return message_; }
    // 0 to 100, or -1 before the first progress.
    int percent() const { return percent_; }

signals:
    // An outside job started or moved on.
    void changed();
    // The outside job ended: its lock is gone. jobId is empty when it wrote no progress.
    void ended(const QString& kind, const QString& jobId);

private:
    void poll();

    std::filesystem::path folder_;
    std::function<bool()> ownJob_;
    QTimer timer_;
    bool active_ = false;
    QString kind_;
    QString jobId_;
    QString message_;
    int percent_ = -1;
};

}
