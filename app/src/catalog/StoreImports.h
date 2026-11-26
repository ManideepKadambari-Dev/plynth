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

// Connects the Sources window and the Fab dialog to the two import queues. It builds the requests and
// hands them over; the queues run them. The window gives it the existing own-files actions.
class StoreImports : public QObject, public IntegrationActions {
    Q_OBJECT
public:
    StoreImports(LibraryHost& host, ImportQueue& assets, MaterialImportQueue& materials, QWidget* dialogParent,
                 QObject* parent = nullptr);

    // Names the window a file picker or the Fab dialog opens over, such as the Sources window that asked for it.
    // Without it they open over the window given at construction.
    void setDialogParent(std::function<QWidget*()> parent) { parentOf_ = std::move(parent); }
    // What the Own files buttons do: the window's own import actions for assets or for materials. origin is the
    // id of the card that asked, so the progress shows on that card.
    void setOwnFiles(std::function<void(bool material, const QString& origin)> action) { ownFiles_ = std::move(action); }
    // What Own files do with files already chosen: the window queues them as its own imports do.
    void setOwnPaths(std::function<void(const QStringList& paths, bool material, const QString& origin)> action)
    {
        ownPaths_ = std::move(action);
    }

    // Queues the item at a pasted address.
    void importAddress(const QString& integrationId, const QString& address, bool material) override;
    // Own files go to the window's actions, Fab zips are checked in the dialog, files of any other source are
    // asked for with that source's file endings.
    void importFiles(const QString& integrationId, bool material) override;
    // What an integration's import button does once the files are chosen. waitForDialog false opens the Fab
    // dialog without waiting, for checks.
    void importChosenFiles(const QString& integrationId, const QStringList& files, bool material, bool waitForDialog = true);
    // Opens the Fab dialog for these zips and queues the ones the user keeps, with the card id as their origin.
    void importFabZips(const QStringList& zips, bool material, const QString& origin);
    // For checks only: opens the Fab dialog without waiting for it, so a picture can be taken. It queues
    // the picks when it is accepted, as importFabZips does.
    FabImportDialog* openFabDialog(const QStringList& zips, bool material, const QString& origin);
    // Opens the pick dialog for the file of a failed queue row and replaces the row with the new import.
    void pickModel(int queueItemId);

signals:
    // A sentence for the status line and the card that asked: nothing could be queued.
    void notice(const QString& text, const QString& origin);

private:
    // The window dialogs open over now.
    QWidget* dialogWindow() const { return parentOf_ ? parentOf_() : dialogParent_; }
    void queuePicks(const FabImportDialog& dialog, const QString& origin);
    LibraryHost& host_;
    ImportQueue& assets_;
    MaterialImportQueue& materials_;
    QWidget* dialogParent_ = nullptr;
    std::function<QWidget*()> parentOf_;
    std::function<void(bool, const QString&)> ownFiles_;
    std::function<void(const QStringList&, bool, const QString&)> ownPaths_;
};

}
