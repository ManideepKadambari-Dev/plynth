#include "MainWindow.h"

#include "building/BuildController.h"
#include "building/ExportController.h"
#include "catalog/ImportQueue.h"
#include "detection/DetectionSession.h"
#include "materials/MaterialImportQueue.h"
#include "materials/MaterialRefresher.h"
#include "setup/SetupCheckDialog.h"
#include "tools/ToolController.h"
#include "update/UpdateController.h"
#include "update/UpdateDialog.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QMenu>
#include <QSignalBlocker>
#include <QStringList>

namespace plynth::app {

// Adds Check for updates, Update and the switch to the Help menu, and gives the controller what it needs.
void MainWindow::buildUpdateActions(QMenu* help)
{
    UpdateController::Hooks hooks;
    hooks.busyReason = [this]() { return updateBusyReason(); };
    hooks.projectFolder = [this]() { return doc_.hasFolder() ? doc_.folder() : std::filesystem::path(); };
    hooks.askToClose = [this]() { return confirmDiscardChanges(); };
    hooks.closeForInstaller = [this]() { closeForInstaller(); };
    updates_->setHooks(std::move(hooks));

    help->addSeparator();
    checkUpdatesAction_ = addMenuAction(help, tr("Check for &updates"),
                                        tr("Ask GitHub whether a newer signed release of Plynth exists"), {});
    connect(checkUpdatesAction_, &QAction::triggered, this, [this]() {
        updates_->checkByHand();
        openUpdateDialog();
    });
    updateAction_ = addMenuAction(help, tr("U&pdate Plynth"),
                                  tr("Download, verify and install the newest signed release. Plynth closes for the "
                                     "installer"),
                                  {});
    connect(updateAction_, &QAction::triggered, this, [this]() {
        updates_->update();
        openUpdateDialog();
    });
    updateAtStartAction_ = addMenuAction(help, tr("Check for updates at &start"),
                                         tr("Once per start, ask GitHub for a newer signed release and show a notice. "
                                            "Nothing about you or your projects is sent"),
                                         {});
    updateAtStartAction_->setCheckable(true);
    connect(updateAtStartAction_, &QAction::triggered, this, [this](bool on) {
        const plynth::Status status = updates_->setCheckAtStart(on);
        if (!status.ok())
            showError(tr("Check for updates at start"),
                      QString::fromUtf8(status.message.data(), static_cast<qsizetype>(status.message.size())));
    });
    connect(updates_, &UpdateController::changed, this, &MainWindow::updateUpdateActions);
    connect(help, &QMenu::aboutToShow, this, &MainWindow::updateUpdateActions);
    updateUpdateActions();
}

// Keeps the switch in the menu equal to the settings.
void MainWindow::updateUpdateActions()
{
    if (updateAtStartAction_ == nullptr)
        return;
    const QSignalBlocker block(updateAtStartAction_);
    updateAtStartAction_->setChecked(updates_->checkAtStart());
}

// Opens the update dialog without blocking the window, or brings it to the front.
void MainWindow::openUpdateDialog()
{
    if (!updateDialog_) {
        updateDialog_ = new UpdateDialog(*updates_, dialogParent());
        updateDialog_->setAttribute(Qt::WA_DeleteOnClose);
    }
    updateDialog_->show();
    updateDialog_->raise();
    updateDialog_->activateWindow();
}

// The work in this window that must finish before an update, or empty. Nothing is cancelled.
QString MainWindow::updateBusyReason() const
{
    QStringList reasons;
    if (builder_ != nullptr && builder_->running())
        reasons << tr("A build is running in this window.");
    if (exporter_ != nullptr && exporter_->running())
        reasons << tr("An export is running in this window.");
    if (queue_ != nullptr && queue_->busy())
        reasons << tr("Assets are being imported.");
    if (materialQueue_ != nullptr && materialQueue_->busy())
        reasons << tr("Materials are being imported.");
    if (materialRefresher_ != nullptr && materialRefresher_->busy())
        reasons << tr("Material pictures are being refreshed.");
    if (tools_ != nullptr && tools_->detection().state() == DetectionSession::State::Running)
        reasons << tr("Wall detection is running.");
    if (reasons.isEmpty())
        return QString();
    reasons << tr("Wait for it to finish or cancel it, then try again. Nothing was stopped.");
    return reasons.join(QLatin1Char(' '));
}

// Closes the app after the installer started. The close questions were already answered.
void MainWindow::closeForInstaller()
{
    closingForInstaller_ = true;
    close();
    QCoreApplication::exit(0);
}

// Starts the check of this start. When the setup check is open (first run, after an update), it waits for it to
// close, so the switch there is seen first.
void MainWindow::startUpdateCheck()
{
    if (screenshotMode_)
        return;
    if (setupDialog_) {
        connect(setupDialog_.data(), &QDialog::finished, this, [this]() { updates_->checkAtStartOfRun(); },
                Qt::SingleShotConnection);
        return;
    }
    updates_->checkAtStartOfRun();
}

// For checks only: shows a canned state. The dialog opens for every state but the notice.
bool MainWindow::showUpdateStateForCheck(const QString& name)
{
    if (!updates_->showCanned(name))
        return false;
    if (name != QStringLiteral("notice"))
        openUpdateDialog();
    return true;
}

}
