#include "MaterialImportQueue.h"

#include "catalog/LibraryHost.h"

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
MaterialImportQueue::MaterialImportQueue(LibraryHost& host, QObject* parent) : QObject(parent), host_(host) {}

// Stops the running item and waits for the worker, so nothing calls back into a deleted queue.
MaterialImportQueue::~MaterialImportQueue()
{
    for (MaterialQueueItem& item : items_)
        item.stop.request_stop();
    worker_ = std::jthread();
}

// Adds an import with the fields the request holds.
int MaterialImportQueue::enqueueImport(const plynth::MaterialImportRequest& request)
{
    MaterialQueueItem item;
    item.kind = MaterialQueueItem::Kind::Import;
    item.title = QString::fromStdWString(request.source.file.filename().wstring());
    item.request = request;
    return add(std::move(item));
}

// Adds an entry of the materials inbox.
int MaterialImportQueue::enqueueInbox(const QString& path)
{
    MaterialQueueItem item;
    item.kind = MaterialQueueItem::Kind::Inbox;
    item.title = QFileInfo(path).fileName();
    item.entry = toPath(path);
    return add(std::move(item));
}

// Puts an item at the end and starts it when nothing runs.
int MaterialImportQueue::add(MaterialQueueItem item)
{
    item.id = nextId_++;
    item.state = MaterialQueueItem::State::Waiting;
    item.stage = tr("Waiting");
    const int id = item.id;
    items_.push_back(std::move(item));
    emit itemsChanged();
    startNext();
    return id;
}

// True while an item is waiting or running.
bool MaterialImportQueue::busy() const
{
    return std::any_of(items_.begin(), items_.end(),
                       [](const MaterialQueueItem& item) { return item.state != MaterialQueueItem::State::Failed; });
}

// True when an inbox entry with this path is queued or running.
bool MaterialImportQueue::hasEntry(const QString& path) const
{
    const std::filesystem::path entry = toPath(path);
    return std::any_of(items_.begin(), items_.end(), [&](const MaterialQueueItem& item) {
        return item.kind == MaterialQueueItem::Kind::Inbox && item.entry == entry &&
               item.state != MaterialQueueItem::State::Failed;
    });
}

// Stops a running item, or removes a waiting or failed one.
void MaterialImportQueue::cancel(int id)
{
    const auto found = std::find_if(items_.begin(), items_.end(), [id](const MaterialQueueItem& item) { return item.id == id; });
    if (found == items_.end())
        return;
    if (found->state == MaterialQueueItem::State::Running) {
        found->stage = tr("Stopping");
        found->stop.request_stop();
    } else {
        items_.erase(found);
    }
    emit itemsChanged();
}

// Starts the first waiting item on the worker thread.
void MaterialImportQueue::startNext()
{
    if (running_ || !host_.hasLibrary())
        return;
    MaterialQueueItem* next = nullptr;
    for (MaterialQueueItem& item : items_)
        if (item.state == MaterialQueueItem::State::Waiting) {
            next = &item;
            break;
        }
    if (next == nullptr)
        return;
    next->state = MaterialQueueItem::State::Running;
    next->stage = tr("Preparing");
    running_ = true;
    emit itemsChanged();

    const int id = next->id;
    const MaterialQueueItem::Kind kind = next->kind;
    const plynth::MaterialImportRequest request = next->request;
    const std::filesystem::path entry = next->entry;
    const std::stop_token stop = next->stop.get_token();
    const plynth::LibraryPaths paths = host_.paths();
    const plynth::MachineSettings settings = host_.settings();
    if (blenderFor_ != settings)
        blender_.reset();
    const std::optional<plynth::BlenderInstall> cached = blender_;
    worker_ = std::jthread([this, id, kind, request, entry, stop, paths, settings, cached]() {
        plynth::MaterialImportOutcome outcome;
        std::optional<plynth::BlenderInstall> chosen = cached;
        plynth::Status opened;
        std::unique_ptr<plynth::MaterialCatalog> catalog = plynth::MaterialCatalog::open(paths, opened);
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
                if (kind == MaterialQueueItem::Kind::Import)
                    outcome = plynth::importMaterial(*catalog, context, request, progress, stop);
                else
                    outcome = plynth::importMaterialInboxEntry(*catalog, context, entry, progress, stop);
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
void MaterialImportQueue::onProgress(int id, const plynth::ImportProgress& progress)
{
    const auto found = std::find_if(items_.begin(), items_.end(), [id](const MaterialQueueItem& item) { return item.id == id; });
    if (found == items_.end())
        return;
    switch (progress.stage) {
    case plynth::ImportStage::Preparing:
        found->stage = tr("Preparing");
        found->percent = 2;
        break;
    case plynth::ImportStage::Blender: {
        const plynth::BlenderProgress& b = progress.blender;
        found->stage = b.message.empty() ? tr("Blender is working") : fromCore(b.message);
        found->percent = b.count > 0 ? 5 + 90 * std::clamp(b.index, 0, b.count) / b.count : 5;
        break;
    }
    case plynth::ImportStage::Saving:
        found->stage = tr("Saving");
        found->percent = 96;
        break;
    case plynth::ImportStage::Done:
        found->percent = 100;
        break;
    }
    emit itemsChanged();
}

// Handles the end of the running item: removes it, or keeps it with the failure.
void MaterialImportQueue::onDone(int id, const plynth::MaterialImportOutcome& outcome)
{
    running_ = false;
    const auto found = std::find_if(items_.begin(), items_.end(), [id](const MaterialQueueItem& item) { return item.id == id; });
    if (found == items_.end()) {
        startNext();
        return;
    }
    const bool cancelled = outcome.status.code == plynth::ErrorCode::Cancelled && found->stop.stop_requested();
    if (outcome.status.ok() || cancelled) {
        items_.erase(found);
        emit itemsChanged();
        if (!cancelled)
            emit finished(true, fromCore(outcome.materialId), QString(), outcome.skipped, outcome.thumbnailFailed);
        startNext();
        return;
    }
    found->state = MaterialQueueItem::State::Failed;
    found->stage = tr("Failed");
    found->message = fromCore(outcome.status.message);
    found->workFolder = QString::fromStdWString(outcome.keptWorkFolder.wstring());
    const QString message = found->message;
    emit itemsChanged();
    emit finished(false, QString(), message, false, true);
    startNext();
}

}
