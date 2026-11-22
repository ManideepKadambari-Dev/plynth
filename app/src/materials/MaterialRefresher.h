#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace plynth::app {

class LibraryHost;

// Cleans materials again and renders their thumbnails, one at a time on a worker thread. The worker opens its
// own MaterialCatalog, so the window's catalog is never used from two threads.
class MaterialRefresher : public QObject {
    Q_OBJECT
public:
    explicit MaterialRefresher(LibraryHost& host, QObject* parent = nullptr);
    ~MaterialRefresher() override;

    // Adds a material to refresh. Does nothing when it is already waiting or running.
    void enqueue(const QString& materialId);
    // Ids waiting or running.
    QStringList busyIds() const;
    bool busy() const { return !waiting_.isEmpty() || running_; }

signals:
    void busyChanged();
    // One refresh ended. message is the failure in plain words, empty when it worked.
    void finished(const QString& materialId, bool ok, const QString& message);

private:
    void startNext();

    LibraryHost& host_;
    QStringList waiting_;
    QString current_;
    bool running_ = false;
    std::jthread worker_;
};

}
