#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>

class QFileSystemWatcher;
class QTimer;

namespace plynth::app {

class ImportQueue;
class LibraryHost;
class MaterialImportQueue;
class MaterialRefresher;

// Watches Library/assets/inbox and Library/assets/items. A file that lands in the inbox is queued once its
// size has stood still for two seconds. A change in items/ runs a rescan after two quiet seconds and queues
// a render for every asset whose asset.blend changed. Library/materials/items is watched the same way, and
// every material the rescan finds changed is handed to the material refresher.
class LibraryWatcher : public QObject {
    Q_OBJECT
public:
    LibraryWatcher(LibraryHost& host, ImportQueue& queue, QObject* parent = nullptr);

    // Turns watching on or off, for example off in screenshot mode.
    void setActive(bool active);
    // Reads the inbox now and rescans now, without waiting. For the Rescan button.
    void rescanNow();
    // Rescans the materials now, without waiting. For the Materials window's Rescan button.
    void rescanMaterialsNow();
    // Where changed materials go to be cleaned and rendered again. Without one they are only reindexed.
    void setMaterialRefresher(MaterialRefresher* refresher) { materialRefresher_ = refresher; }
    // Where files that land in the materials inbox go to be imported.
    void setMaterialQueue(MaterialImportQueue* queue) { materialQueue_ = queue; }

signals:
    void message(const QString& text);

private:
    void follow();
    void checkInbox();
    void rescan(bool announce);
    void rescanMaterials(bool announce);

    LibraryHost& host_;
    ImportQueue& queue_;
    bool active_ = true;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* inboxTimer_ = nullptr;
    QTimer* itemsTimer_ = nullptr;
    QTimer* materialsTimer_ = nullptr;
    MaterialRefresher* materialRefresher_ = nullptr;
    MaterialImportQueue* materialQueue_ = nullptr;
    struct Seen {
        qint64 bytes = -1;
        QDateTime since;
    };
    QHash<QString, Seen> seen_;
    QSet<QString> queued_;
};

}
