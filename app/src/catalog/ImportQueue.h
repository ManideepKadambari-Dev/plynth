#pragma once

#include <plynth/core/catalog/AssetImport.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace plynth::app {

class LibraryHost;

// One line of the import queue: what it is, how far it got, or why it failed.
struct QueueItem {
    enum class Kind { Import, Inbox, Refresh };
    enum class State { Waiting, Running, Failed };

    int id = 0;
    Kind kind = Kind::Import;
    State state = State::Waiting;
    QString title;       // file name, or the asset name for a render
    QString assetId;     // for a render
    QString stage;       // "Waiting", "Preparing", the worker's step text, "Saving"
    int percent = 0;     // 0 to 100
    QString message;     // the failure, in plain words
    QString workFolder;  // set when a Blender failure kept its work folder for the log
    plynth::ImportRequest request;
    std::filesystem::path entry;
    std::stop_source stop;
};

// Runs imports, inbox entries and thumbnail renders one at a time on a worker thread. The worker opens its
// own AssetCatalog, so the window's catalog is never used from two threads.
class ImportQueue : public QObject {
    Q_OBJECT
public:
    explicit ImportQueue(LibraryHost& host, QObject* parent = nullptr);
    ~ImportQueue() override;

    // Adds a file or folder to import with default fields. Returns the item id.
    int enqueueImport(const QString& path);
    // Adds an entry of the library inbox.
    int enqueueInbox(const QString& path);
    // Adds a measure and thumbnail render of an asset. Does nothing when one is already waiting.
    int enqueueRefresh(const QString& assetId, const QString& assetName);

    const std::vector<QueueItem>& items() const { return items_; }
    // True while an item is waiting or running.
    bool busy() const;
    // Ids of the assets that have a render waiting or running.
    QStringList refreshingAssets() const;
    // True when an inbox entry with this path is already queued or running.
    bool hasEntry(const QString& path) const;
    // Stops a running item, or removes a waiting or failed one.
    void cancel(int id);
    // Removes a failed item from the list.
    void dismiss(int id);

signals:
    void itemsChanged();
    // An item ended. ok false with a message for a failure; cancelled items are removed without this.
    void finished(int kind, bool ok, const QString& assetId, const QString& message, bool thumbnailFailed);

private:
    int add(QueueItem item);
    void startNext();
    void onProgress(int id, const plynth::ImportProgress& progress);
    void onDone(int id, const plynth::ImportOutcome& outcome);
    QueueItem* find(int id);

    LibraryHost& host_;
    std::vector<QueueItem> items_;
    int nextId_ = 1;
    bool running_ = false;
    std::jthread worker_;
};

}
