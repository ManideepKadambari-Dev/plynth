#include "MaterialRefresher.h"

#include "catalog/LibraryHost.h"

#include <plynth/core/catalog/MaterialImport.h>

#include <QMetaObject>

namespace plynth::app {

MaterialRefresher::MaterialRefresher(LibraryHost& host, QObject* parent) : QObject(parent), host_(host) {}

// The worker is asked to stop and joined by the thread's own destructor.
MaterialRefresher::~MaterialRefresher() = default;

// Queues a material and starts the worker when it is idle.
void MaterialRefresher::enqueue(const QString& materialId)
{
    if (materialId.isEmpty() || waiting_.contains(materialId) || (running_ && current_ == materialId))
        return;
    waiting_.append(materialId);
    emit busyChanged();
    startNext();
}

// The ids waiting or running, running one first.
QStringList MaterialRefresher::busyIds() const
{
    QStringList ids;
    if (running_)
        ids << current_;
    ids << waiting_;
    return ids;
}

// Takes the next id and refreshes it on the worker thread with its own catalog and the Blender the settings name.
void MaterialRefresher::startNext()
{
    if (running_ || waiting_.isEmpty() || !host_.hasLibrary())
        return;
    const QString id = waiting_.takeFirst();
    current_ = id;
    running_ = true;
    const plynth::LibraryPaths paths = host_.paths();
    const plynth::MachineSettings settings = host_.settings();
    worker_ = std::jthread([this, id, paths, settings](std::stop_token stop) {
        plynth::MaterialImportOutcome outcome;
        plynth::Status opened;
        std::unique_ptr<plynth::MaterialCatalog> catalog = plynth::MaterialCatalog::open(paths, opened);
        if (!catalog) {
            outcome.status = opened;
        } else {
            const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, stop);
            if (!choice.status.ok()) {
                outcome.status = choice.status;
            } else {
                plynth::ImportContext context;
                context.library = paths;
                context.blender = choice.install;
                context.settings = settings;
                outcome = plynth::refreshMaterial(*catalog, context, id.toStdString(), {}, stop);
            }
        }
        if (stop.stop_requested())
            return;
        const QString message = QString::fromStdString(outcome.status.message);
        const bool ok = outcome.status.ok();
        QMetaObject::invokeMethod(this, [this, id, ok, message]() {
            running_ = false;
            current_.clear();
            emit busyChanged();
            emit finished(id, ok, message);
            startNext();
        }, Qt::QueuedConnection);
    });
}

}
