#include "LibraryWatcher.h"

#include "ImportQueue.h"
#include "LibraryHost.h"

#include <plynth/core/catalog/AssetImport.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace plynth::app {

namespace {

constexpr int kSettleMs = 2000;
constexpr int kInboxPollMs = 1000;
constexpr int kMaxWatchedItems = 400;

// Turns a standard path into Qt text.
QString toQt(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

// Adds up the size of a file, or of every file in a folder.
qint64 totalBytes(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return info.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

}

// Starts watching the library the host has open.
LibraryWatcher::LibraryWatcher(LibraryHost& host, ImportQueue& queue, QObject* parent)
    : QObject(parent), host_(host), queue_(queue)
{
    watcher_ = new QFileSystemWatcher(this);
    inboxTimer_ = new QTimer(this);
    inboxTimer_->setInterval(kInboxPollMs);
    connect(inboxTimer_, &QTimer::timeout, this, &LibraryWatcher::checkInbox);
    itemsTimer_ = new QTimer(this);
    itemsTimer_->setSingleShot(true);
    itemsTimer_->setInterval(kSettleMs);
    connect(itemsTimer_, &QTimer::timeout, this, [this]() { rescan(false); });

    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        if (!active_ || !host_.hasLibrary())
            return;
        if (QDir::cleanPath(path) == QDir::cleanPath(toQt(host_.paths().assetInbox)))
            inboxTimer_->start();
        else
            itemsTimer_->start();
        if (QDir::cleanPath(path) == QDir::cleanPath(toQt(host_.paths().assetItems)))
            follow();
    });
    connect(&host_, &LibraryHost::libraryChanged, this, &LibraryWatcher::follow);
    follow();
}

// Turns watching on or off.
void LibraryWatcher::setActive(bool active)
{
    active_ = active;
    follow();
}

// Points the watcher at the inbox, the items folder and each item folder.
void LibraryWatcher::follow()
{
    if (!watcher_->directories().isEmpty())
        watcher_->removePaths(watcher_->directories());
    inboxTimer_->stop();
    if (!active_ || !host_.hasLibrary())
        return;
    const plynth::LibraryPaths& paths = host_.paths();
    watcher_->addPath(toQt(paths.assetInbox));
    watcher_->addPath(toQt(paths.assetItems));
    int count = 0;
    for (const QFileInfo& info : QDir(toQt(paths.assetItems)).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (info.fileName().startsWith(QLatin1Char('.')))
            continue;
        if (++count > kMaxWatchedItems)
            break;
        watcher_->addPath(info.absoluteFilePath());
    }
    inboxTimer_->start();
}

// Queues every inbox entry whose size has not changed for two seconds.
void LibraryWatcher::checkInbox()
{
    if (!active_ || !host_.hasLibrary()) {
        inboxTimer_->stop();
        return;
    }
    const std::vector<std::filesystem::path> entries = plynth::inboxEntries(host_.paths());
    QSet<QString> present;
    const QDateTime now = QDateTime::currentDateTime();
    for (const std::filesystem::path& entry : entries) {
        const QString path = toQt(entry);
        present.insert(path);
        if (queued_.contains(path) || queue_.hasEntry(path))
            continue;
        const qint64 bytes = totalBytes(path);
        Seen& seen = seen_[path];
        if (seen.bytes != bytes) {
            seen.bytes = bytes;
            seen.since = now;
            continue;
        }
        if (seen.since.msecsTo(now) >= kSettleMs) {
            queued_.insert(path);
            queue_.enqueueInbox(path);
        }
    }
    for (auto it = seen_.begin(); it != seen_.end();)
        it = present.contains(it.key()) ? std::next(it) : seen_.erase(it);
    for (auto it = queued_.begin(); it != queued_.end();)
        it = present.contains(*it) ? std::next(it) : queued_.erase(it);
    if (present.isEmpty())
        inboxTimer_->stop();
    else if (!inboxTimer_->isActive())
        inboxTimer_->start();
}

// Rebuilds the index from items/ and queues a render for each changed asset.blend.
void LibraryWatcher::rescan(bool announce)
{
    if (!host_.hasLibrary())
        return;
    const plynth::RescanReport report = host_.catalog()->rescan();
    bool changed = report.added + report.updated + report.removed > 0;
    for (const std::string& id : report.needsRefresh) {
        const std::optional<plynth::AssetRecord> record = host_.catalog()->asset(id);
        queue_.enqueueRefresh(QString::fromStdString(id),
                              record ? QString::fromStdString(record->name) : QString::fromStdString(id));
        changed = true;
    }
    if (changed)
        host_.assetsEdited();
    follow();
    if (announce)
        emit message(tr("Rescan: %1 added, %2 changed, %3 removed, %4 to render again.")
                     .arg(report.added)
                     .arg(report.updated)
                     .arg(report.removed)
                     .arg(report.needsRefresh.size()));
}

// Reads the inbox and rescans at once.
void LibraryWatcher::rescanNow()
{
    queued_.clear();
    checkInbox();
    rescan(true);
}

}
