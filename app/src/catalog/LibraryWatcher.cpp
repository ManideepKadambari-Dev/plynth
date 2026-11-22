#include "LibraryWatcher.h"

#include "ImportQueue.h"
#include "LibraryHost.h"
#include "materials/MaterialImportQueue.h"
#include "materials/MaterialRefresher.h"

#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/MaterialImport.h>

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
    materialsTimer_ = new QTimer(this);
    materialsTimer_->setSingleShot(true);
    materialsTimer_->setInterval(kSettleMs);
    connect(materialsTimer_, &QTimer::timeout, this, [this]() { rescanMaterials(false); });

    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        if (!active_ || !host_.hasLibrary())
            return;
        const QString clean = QDir::cleanPath(path);
        const QString materialItems = QDir::cleanPath(toQt(host_.paths().materialItems));
        if (clean == materialItems || clean.startsWith(materialItems + QLatin1Char('/')))
            materialsTimer_->start();
        else if (clean == QDir::cleanPath(toQt(host_.paths().assetInbox)) ||
                 clean == QDir::cleanPath(toQt(host_.paths().materialInbox)))
            inboxTimer_->start();
        else
            itemsTimer_->start();
        if (clean == QDir::cleanPath(toQt(host_.paths().assetItems)) || clean == materialItems)
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
    watcher_->addPath(toQt(paths.materialInbox));
    watcher_->addPath(toQt(paths.assetItems));
    watcher_->addPath(toQt(paths.materialItems));
    int count = 0;
    for (const QFileInfo& info : QDir(toQt(paths.assetItems)).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (info.fileName().startsWith(QLatin1Char('.')))
            continue;
        if (++count > kMaxWatchedItems)
            break;
        watcher_->addPath(info.absoluteFilePath());
    }
    count = 0;
    for (const QFileInfo& info : QDir(toQt(paths.materialItems)).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
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
    std::vector<std::filesystem::path> entries = plynth::inboxEntries(host_.paths());
    const size_t assetEntries = entries.size();
    if (materialQueue_ != nullptr) {
        const std::vector<std::filesystem::path> more = plynth::materialInboxEntries(host_.paths());
        entries.insert(entries.end(), more.begin(), more.end());
    }
    QSet<QString> present;
    const QDateTime now = QDateTime::currentDateTime();
    size_t index = 0;
    for (const std::filesystem::path& entry : entries) {
        const bool forMaterials = index++ >= assetEntries;
        const QString path = toQt(entry);
        present.insert(path);
        if (queued_.contains(path) || (forMaterials ? materialQueue_->hasEntry(path) : queue_.hasEntry(path)))
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
            if (forMaterials)
                materialQueue_->enqueueInbox(path);
            else
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

// Rebuilds the materials index from items/ and queues a refresh for each material that needs one.
void LibraryWatcher::rescanMaterials(bool announce)
{
    if (!host_.hasLibrary() || host_.materialCatalog() == nullptr)
        return;
    const plynth::RescanReport report = host_.materialCatalog()->rescan();
    bool changed = report.added + report.updated + report.removed > 0;
    for (const std::string& id : report.needsRefresh) {
        if (materialRefresher_ != nullptr)
            materialRefresher_->enqueue(QString::fromStdString(id));
        changed = true;
    }
    if (changed)
        host_.materialsEdited();
    follow();
    if (announce)
        emit message(tr("Rescan of materials: %1 added, %2 changed, %3 removed, %4 to render again.")
                     .arg(report.added)
                     .arg(report.updated)
                     .arg(report.removed)
                     .arg(report.needsRefresh.size()));
}

// Rescans the materials at once.
void LibraryWatcher::rescanMaterialsNow()
{
    rescanMaterials(true);
}

// Reads the inbox and rescans at once.
void LibraryWatcher::rescanNow()
{
    queued_.clear();
    checkInbox();
    rescan(true);
}

}
