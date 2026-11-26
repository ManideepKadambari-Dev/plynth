#pragma once

#include <plynth/core/catalog/MaterialImport.h>

#include <QObject>
#include <QString>

#include <filesystem>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace plynth::app {

class LibraryHost;

// One line of the material import queue.
struct MaterialQueueItem {
    enum class Kind { Import, Inbox };
    enum class State { Waiting, Running, Failed };

    int id = 0;
    Kind kind = Kind::Import;
    State state = State::Waiting;
    QString title;
    QString stage;
    int percent = 0;
    QString message;
    QString workFolder;
    QString origin; // the id of the source card in the Sources window that started it, empty otherwise
    plynth::MaterialImportRequest request;
    std::filesystem::path entry;
    std::stop_source stop;
};

// Runs material imports and inbox entries one at a time on a worker thread. The worker opens its own
// MaterialCatalog, so the window's catalog is never used from two threads.
class MaterialImportQueue : public QObject {
    Q_OBJECT
public:
    explicit MaterialImportQueue(LibraryHost& host, QObject* parent = nullptr);
    ~MaterialImportQueue() override;

    // Adds an import with the fields the request holds. Returns the item id. origin names the source card in
    // the Sources window that started it, so its progress shows there.
    int enqueueImport(const plynth::MaterialImportRequest& request, const QString& origin = QString());
    // Adds an entry of the materials inbox.
    int enqueueInbox(const QString& path);

    const std::vector<MaterialQueueItem>& items() const { return items_; }
    // True while an item is waiting or running.
    bool busy() const;
    // True when an inbox entry with this path is already queued or running.
    bool hasEntry(const QString& path) const;
    // Stops a running item, or removes a waiting or failed one.
    void cancel(int id);

signals:
    void itemsChanged();
    // An item ended. Cancelled items are removed without this. skipped means the id was already there.
    // origin is the source card that started it, empty for any other item.
    void finished(bool ok, const QString& materialId, const QString& message, bool skipped, bool thumbnailFailed,
                  const QString& origin);

private:
    int add(MaterialQueueItem item);
    void startNext();
    void onProgress(int id, const plynth::ImportProgress& progress);
    void onDone(int id, const plynth::MaterialImportOutcome& outcome);

    LibraryHost& host_;
    std::vector<MaterialQueueItem> items_;
    int nextId_ = 1;
    bool running_ = false;
    std::optional<plynth::BlenderInstall> blender_;
    plynth::MachineSettings blenderFor_;
    std::jthread worker_;
};

}
