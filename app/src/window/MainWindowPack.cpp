#include "MainWindow.h"

#include "StatusBar.h"
#include "catalog/LibraryHost.h"
#include "dialogs/PackDialog.h"

#include <plynth/core/Pack.h>

#include <QApplication>
#include <QMessageBox>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// File > Pack project: saves first, then copies the library items the project uses into its pack folder through
// the same packProject as "plynth pack", and lists the result.
void MainWindow::packProjectDialog()
{
    if (!doc_.hasFolder() || !host_->hasLibrary()) {
        statusBar_->showMessage(tr("Open a saved project and set a library folder first."));
        return;
    }
    if (doc_.isModified()) {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(dialogParent(), tr("Pack project"), tr("The project has to be saved before it is packed. Save it now?"),
                                  QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
        if (answer != QMessageBox::Save || !saveDocument())
            return;
    }
    plynth::PackRequest request;
    request.projectFolder = doc_.folder();
    request.contents = plynth::packContents(doc_.project(), host_->paths());
    request.library = host_->paths();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const plynth::PackReport report = plynth::packProject(request);
    QApplication::restoreOverrideCursor();
    if (!report.status.ok()) {
        showError(tr("Pack project"), fromCore(report.status.message));
        return;
    }
    PackDialog dialog(report, dialogParent());
    dialog.exec();
}

// Shows one of the two dialogs without blocking, for the screenshot checks. Pack runs as a check, so nothing is
// written; unpack only plans.
void MainWindow::showPackDialogForCheck(const QString& kind)
{
    if (!doc_.hasFolder() || !host_->hasLibrary()) {
        statusBar_->showMessage(tr("Open a saved project and set a library folder first."));
        return;
    }
    delete checkPackDialog_.data();
    if (kind == QStringLiteral("unpack")) {
        const plynth::UnpackPlan plan = plynth::planUnpack(doc_.folder(), host_->paths());
        if (!plan.status.ok()) {
            statusBar_->showMessage(fromCore(plan.status.message));
            return;
        }
        checkPackDialog_ = new PackDialog(plan, this);
    } else {
        plynth::PackRequest request;
        request.projectFolder = doc_.folder();
        request.contents = plynth::packContents(doc_.project(), host_->paths());
        request.library = host_->paths();
        request.checkOnly = true;
        const plynth::PackReport report = plynth::packProject(request);
        if (!report.status.ok()) {
            statusBar_->showMessage(fromCore(report.status.message));
            return;
        }
        checkPackDialog_ = new PackDialog(report, this);
    }
    checkPackDialog_->setAttribute(Qt::WA_DeleteOnClose);
    checkPackDialog_->show();
}

// File > Unpack into library: shows what the project's pack would add, and on Unpack copies it in through the
// same unpackProject as "plynth unpack". A conflict keeps the library's items.
void MainWindow::unpackProjectDialog()
{
    if (!doc_.hasFolder() || !host_->hasLibrary()) {
        statusBar_->showMessage(tr("Open a project with a pack and set a library folder first."));
        return;
    }
    const plynth::UnpackPlan plan = plynth::planUnpack(doc_.folder(), host_->paths());
    if (!plan.status.ok()) {
        showError(tr("Unpack into library"), fromCore(plan.status.message));
        return;
    }
    PackDialog dialog(plan, dialogParent());
    if (dialog.exec() != QDialog::Accepted)
        return;
    plynth::UnpackOptions options;
    options.keepLibrary = dialog.keepsLibrary();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const plynth::UnpackReport report = plynth::unpackProject(doc_.folder(), host_->paths(), options);
    QApplication::restoreOverrideCursor();
    if (!report.status.ok()) {
        showError(tr("Unpack into library"), fromCore(report.status.message));
        return;
    }
    host_->roomTypesEdited();
    host_->assetsEdited();
    host_->materialsEdited();
    statusBar_->showMessage(report.complete ? tr("Unpacked %1 items into the library.").arg(report.added)
                                            : tr("Unpacked %1 items. Some are still to get or missing; see the list.")
                                                  .arg(report.added),
                            false);
}

}
