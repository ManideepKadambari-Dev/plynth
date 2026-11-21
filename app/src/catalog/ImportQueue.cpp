#include "ImportQueue.h"

#include "LibraryHost.h"

#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>

namespace plynth::app {

namespace {

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns a Qt path into a standard path without losing non English letters.
std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

}

// Starts with an empty queue.
ImportQueue::ImportQueue(LibraryHost& host, QObject* parent) : QObject(parent), host_(host)
{
}

// Stops the running item and waits for the worker, so nothing calls back into a deleted queue.
ImportQueue::~ImportQueue()
{
    for (QueueItem& item : items_)
        item.stop.request_stop();
    worker_ = std::jthread();
}

// Adds a file or folder to import with default fields.
int ImportQueue::enqueueImport(const QString& path)
{
    QueueItem item;
    item.kind = QueueItem::Kind::Import;
    item.title = QFileInfo(path).fileName();
    item.request.source.file = toPath(path);
    return add(std::move(item));
}

// Adds an entry of the library inbox.
int ImportQueue::enqueueInbox(const QString& path)
{
    QueueItem item;
    item.kind = QueueItem::Kind::Inbox;
    item.title = QFileInfo(path).fileName();
    item.entry = toPath(path);
    return add(std::move(item));
}

// Adds a render of an asset unless one is already waiting.
int ImportQueue::enqueueRefresh(const QString& assetId, const QString& assetName)
{
    for (const QueueItem& item : items_)
        if (item.kind == QueueItem::Kind::Refresh && item.assetId == assetId && item.state != QueueItem::State::Failed)
            return item.id;
    QueueItem item;
    item.kind = QueueItem::Kind::Refresh;
    item.title = tr("Render of %1").arg(assetName);
    item.assetId = assetId;
    return add(std::move(item));
}

// Puts an item at the end and starts it when nothing runs.
int ImportQueue::add(QueueItem item)
{
    item.id = nextId_++;
    item.state = QueueItem::State::Waiting;
    item.stage = tr("Waiting");
    const int id = item.id;
    items_.push_back(std::move(item));
    emit itemsChanged();
    startNext();
    return id;
}

// True while an item is waiting or running.
bool ImportQueue::busy() const
{
    return std::any_of(items_.begin(), items_.end(),
                       [](const QueueItem& item) { return item.state != QueueItem::State::Failed; });
}

// Ids of the assets with a render waiting or running.
QStringList ImportQueue::refreshingAssets() const
{
    QStringList ids;
    for (const QueueItem& item : items_)
        if (item.kind == QueueItem::Kind::Refresh && item.state != QueueItem::State::Failed)
            ids << item.assetId;
    return ids;
}

// True when an inbox entry with this path is queued or running.
bool ImportQueue::hasEntry(const QString& path) const
{
    const std::filesystem::path entry = toPath(path);
    return std::any_of(items_.begin(), items_.end(), [&](const QueueItem& item) {
        return item.kind == QueueItem::Kind::Inbox && item.entry == entry && item.state != QueueItem::State::Failed;
    });
}

// Finds an item by id.
QueueItem* ImportQueue::find(int id)
{
    for (QueueItem& item : items_)
        if (item.id == id)
            return &item;
    return nullptr;
}

// Stops a running item, or removes a waiting or failed one.
void ImportQueue::cancel(int id)
{
    QueueItem* item = find(id);
    if (item == nullptr)
        return;
    if (item->state == QueueItem::State::Running) {
        item->stage = tr("Stopping");
        item->stop.request_stop();
        emit itemsChanged();
        return;
    }
    dismiss(id);
}

// Removes an item that is not running from the list.
void ImportQueue::dismiss(int id)
{
    const auto found = std::find_if(items_.begin(), items_.end(), [id](const QueueItem& item) { return item.id == id; });
    if (found == items_.end() || found->state == QueueItem::State::Running)
        return;
    items_.erase(found);
    emit itemsChanged();
}

// Starts the first waiting item on the worker thread.
void ImportQueue::startNext()
{
    if (running_ || !host_.hasLibrary())
        return;
    QueueItem* next = nullptr;
    for (QueueItem& item : items_)
        if (item.state == QueueItem::State::Waiting) {
            next = &item;
            break;
        }
    if (next == nullptr)
        return;
    next->state = QueueItem::State::Running;
    next->stage = tr("Preparing");
    running_ = true;
    emit itemsChanged();

    const int id = next->id;
    const QueueItem::Kind kind = next->kind;
    const plynth::ImportRequest request = next->request;
    const std::filesystem::path entry = next->entry;
    const std::string assetId = next->assetId.toStdString();
    const std::stop_token stop = next->stop.get_token();
    const plynth::LibraryPaths paths = host_.paths();
    const plynth::MachineSettings settings = host_.settings();
    if (blenderFor_ != settings)
        blender_.reset();
    const std::optional<plynth::BlenderInstall> cached = blender_;
    worker_ = std::jthread([this, id, kind, request, entry, assetId, stop, paths, settings, cached]() {
        plynth::ImportOutcome outcome;
        std::optional<plynth::BlenderInstall> chosen = cached;
        plynth::Status opened;
        std::unique_ptr<plynth::AssetCatalog> catalog = plynth::AssetCatalog::open(paths, opened);
        if (!catalog) {
            outcome.status = opened;
        } else {
            plynth::Status chosenStatus;
            if (!chosen) {
                const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, stop);
                chosenStatus = choice.status;
                if (choice.status.ok())
                    chosen = choice.install;
            }
            if (!chosenStatus.ok()) {
                outcome.status = chosenStatus;
            } else {
                plynth::ImportContext context;
                context.library = paths;
                context.blender = *chosen;
                context.settings = settings;
                const auto progress = [this, id](const plynth::ImportProgress& p) {
                    QMetaObject::invokeMethod(this, [this, id, p]() { onProgress(id, p); }, Qt::QueuedConnection);
                };
                if (kind == QueueItem::Kind::Import)
                    outcome = plynth::importAsset(*catalog, context, request, progress, stop);
                else if (kind == QueueItem::Kind::Inbox)
                    outcome = plynth::importInboxEntry(*catalog, context, entry, progress, stop);
                else
                    outcome = plynth::refreshAsset(*catalog, context, assetId, progress, stop);
            }
        }
        QMetaObject::invokeMethod(this, [this, id, outcome, chosen, settings]() {
            if (outcome.status.code == plynth::ErrorCode::BlenderNotFound) {
                blender_.reset();
            } else if (chosen) {
                blender_ = chosen;
                blenderFor_ = settings;
            }
            onDone(id, outcome);
        }, Qt::QueuedConnection);
    });
}

// Updates the stage text and the bar of the running item.
void ImportQueue::onProgress(int id, const plynth::ImportProgress& progress)
{
    QueueItem* item = find(id);
    if (item == nullptr)
        return;
    switch (progress.stage) {
    case plynth::ImportStage::Preparing:
        item->stage = tr("Preparing");
        item->percent = 2;
        break;
    case plynth::ImportStage::Blender: {
        const plynth::BlenderProgress& b = progress.blender;
        item->stage = b.message.empty() ? tr("Blender is working") : fromCore(b.message);
        item->percent = b.count > 0 ? 5 + 90 * std::clamp(b.index, 0, b.count) / b.count : 5;
        break;
    }
    case plynth::ImportStage::Saving:
        item->stage = tr("Saving");
        item->percent = 96;
        break;
    case plynth::ImportStage::Done:
        item->percent = 100;
        break;
    }
    emit itemsChanged();
}

// Handles the end of the running item: removes it, or keeps it with the failure.
void ImportQueue::onDone(int id, const plynth::ImportOutcome& outcome)
{
    running_ = false;
    const auto found = std::find_if(items_.begin(), items_.end(), [id](const QueueItem& item) { return item.id == id; });
    if (found == items_.end()) {
        startNext();
        return;
    }
    const int kind = static_cast<int>(found->kind);
    // A stop the user did not ask for, such as a timeout, is a failure and keeps its row with the message.
    const bool cancelled = outcome.status.code == plynth::ErrorCode::Cancelled && found->stop.stop_requested();
    QString assetId = fromCore(outcome.assetId);
    if (assetId.isEmpty())
        assetId = found->assetId;
    if (outcome.status.ok() || cancelled) {
        items_.erase(found);
        emit itemsChanged();
        if (!cancelled)
            emit finished(kind, true, assetId, QString(), outcome.thumbnailFailed);
        startNext();
        return;
    }
    // A render that failed leaves the asset marked Failed, which the grid shows. The row says why.
    found->state = QueueItem::State::Failed;
    found->stage = tr("Failed");
    found->message = fromCore(outcome.status.message);
    found->workFolder = QString::fromStdWString(outcome.keptWorkFolder.wstring());
    const QString message = found->message;
    emit itemsChanged();
    emit finished(kind, false, assetId, message, true);
    startNext();
}

}
