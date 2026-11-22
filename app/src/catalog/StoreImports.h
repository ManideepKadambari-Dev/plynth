#pragma once

#include "panels/IntegrationsPanel.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QDialog;
class QWidget;

namespace plynth::app {

class FabImportDialog;
class ImportQueue;
class LibraryHost;
class MaterialImportQueue;

// Connects the Integrations tab and the Fab dialog to the two import queues. It builds the requests and
// hands them over; the queues run them. The window gives it the existing own-files actions.
class StoreImports : public QObject, public IntegrationActions {
    Q_OBJECT
public:
    StoreImports(LibraryHost& host, ImportQueue& assets, MaterialImportQueue& materials, QWidget* dialogParent,
                 QObject* parent = nullptr);

    // What the Own files buttons do: the window's own import actions for assets or for materials.
    void setOwnFiles(std::function<void(bool material)> action) { ownFiles_ = std::move(action); }

    // Queues the item at a pasted address.
    void importAddress(const QString& integrationId, const QString& address, bool material) override;
    // Own files go to the window's actions; Fab zips are asked for and checked in the dialog.
    void importFiles(const QString& integrationId, bool material) override;
    // Opens the Fab dialog for these zips and queues the ones the user keeps.
    void importFabZips(const QStringList& zips, bool material);
    // For checks only: opens the Fab dialog without waiting for it, so a picture can be taken. It queues
    // the picks when it is accepted, as importFabZips does.
    FabImportDialog* openFabDialog(const QStringList& zips, bool material);
    // Opens the Fab dialog for the zip of a failed queue row and replaces the row with the new import.
    void pickModel(int queueItemId);

signals:
    // A sentence for the status line: nothing could be queued.
    void notice(const QString& text);

private:
    void queuePicks(const FabImportDialog& dialog);
    LibraryHost& host_;
    ImportQueue& assets_;
    MaterialImportQueue& materials_;
    QWidget* dialogParent_ = nullptr;
    std::function<void(bool)> ownFiles_;
};

}
