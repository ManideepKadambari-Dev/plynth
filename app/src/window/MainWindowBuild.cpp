#include "MainWindow.h"

#include "document/FloorFocus.h"

#include "BuildBar.h"
#include "StatusBar.h"
#include "building/BuildController.h"
#include "building/BuildPanel.h"
#include "building/ExportController.h"
#include "building/OutsideJobWatch.h"
#include "panels/ExportPanel.h"
#include "canvas/PlanCanvas.h"
#include "catalog/LibraryHost.h"
#include "preview/OverlayRenderer.h"
#include "preview/GraphicsProbe.h"
#include "preview/PreviewView.h"
#include "preview/RenderHost.h"

#include <plynth/core/Blender.h>
#include <plynth/core/Render.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/SetupCheck.h>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// True when a required item of the setup check blocks building, with the sentence that says what to do.
bool MainWindow::buildBlocked(QString* reason) const
{
    const plynth::SetupReport quick =
        plynth::quickSetupCheck(host_->settings(), host_->settingsExisted(), appGraphics());
    for (const plynth::SetupItem& item : quick.items)
        if (item.blocks()) {
            if (reason != nullptr)
                *reason = fromCore(item.name) + QStringLiteral(": ") + fromCore(item.action.empty() ? item.detail : item.action);
            return true;
        }
    return false;
}

// Starts a build of the project as it is in memory.
void MainWindow::startBuild()
{
    if (workRunning())
        return;
    QString reason;
    if (buildBlocked(&reason)) {
        statusBar_->showMessage(reason);
        if (!screenshotMode_)
            openSetupCheck();
        return;
    }
    if (!doc_.hasFolder()) {
        if (screenshotMode_) {
            statusBar_->showMessage(tr("Save the project before building."));
            return;
        }
        const QMessageBox::StandardButton answer =
            QMessageBox::question(dialogParent(), tr("Build"), tr("The project has to be saved before it can be built. Save it now?"),
                                  QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
        if (answer != QMessageBox::Save || !saveDocumentAs() || !doc_.hasFolder())
            return;
    }
    plynth::BuildRequest request;
    request.projectFolder = doc_.folder();
    request.project = doc_.project();
    if (host_->hasLibrary())
        request.library = host_->paths();
    buildProgressText_ = tr("Starting");
    buildProgressPercent_ = 0;
    builder_->start(std::move(request), host_->settings());
    updateBuildUi();
}

// Takes the finished build: keeps its report, opens the 3D page on a good build and loads the new FBX.
void MainWindow::buildFinished(const plynth::BuildReport& report)
{
    if (report.status.code == plynth::ErrorCode::Cancelled) {
        statusBar_->showMessage(tr("The build was cancelled. The last building stays as it was."), false);
    } else {
        report_ = report;
        if (!report.status.ok())
            statusBar_->showMessage(fromCore(report.status.message));
    }
    updateBuildUi();
    if (report.status.ok()) {
        refreshPreview();
        showCentreView(true);
    }
    // A build refused because another program holds the lock leaves that job to be followed.
    outsideJob_->recheck();
}

// True while the app's own build or export runs, or one started by another program on this project.
bool MainWindow::workRunning() const
{
    return (builder_ != nullptr && builder_->running()) || (exporter_ != nullptr && exporter_->running()) ||
           (outsideJob_ != nullptr && outsideJob_->active());
}

// Takes the end of a build or an export another program ran: reads its report and shows it like the app's own.
void MainWindow::outsideJobEnded(const QString& kind, const QString& jobId)
{
    const QString lastBuild = report_ ? fromCore(report_->jobId) : QString();
    if (kind != QStringLiteral("export")) {
        refreshPreview();
        const QString newBuild = report_ ? fromCore(report_->jobId) : QString();
        const bool fresh = report_ && (jobId.isEmpty() ? newBuild != lastBuild : newBuild == jobId);
        if (fresh && report_->status.ok()) {
            statusBar_->showMessage(tr("Built from the command line."), false);
            showCentreView(true);
        } else if (fresh) {
            statusBar_->showMessage(fromCore(report_->status.message));
        } else if (kind == QStringLiteral("build")) {
            statusBar_->showMessage(tr("The build from the command line stopped before it finished. The last building stays as it was."), false);
        }
    }
    if (kind != QStringLiteral("build")) {
        const QString lastExport = exportReport_ ? fromCore(exportReport_->jobId) : QString();
        loadExportReport();
        const QString newExport = exportReport_ ? fromCore(exportReport_->jobId) : QString();
        const bool fresh = exportReport_ && (jobId.isEmpty() ? newExport != lastExport : newExport == jobId);
        if (fresh && exportReport_->status.ok())
            statusBar_->showMessage(exportReport_->exported ? tr("Exported from the command line. The files are in the export folder.")
                                                            : tr("The export checks from the command line ended. Nothing was written."),
                                    false);
        else if (fresh)
            statusBar_->showMessage(fromCore(exportReport_->status.message));
        else if (kind == QStringLiteral("export"))
            statusBar_->showMessage(tr("The export from the command line stopped before it finished. The last export stays as it was."), false);
    }
    updateBuildUi();
}

// Reads build/build.json of the open project, if there is one.
void MainWindow::loadBuildReport()
{
    report_.reset();
    if (doc_.hasFolder()) {
        const plynth::BuildReportRead read = plynth::readBuildReport(doc_.folder());
        if (read.status.ok())
            report_ = read.report;
    }
    updateBuildUi();
}

// Shows the state of the last build and of the Build button in the bar, the menu and the Build report window.
void MainWindow::updateBuildUi()
{
    const bool outside = outsideJob_ != nullptr && outsideJob_->active();
    const bool running = workRunning();
    const bool stale = report_ && report_->status.ok() && doc_.hasFolder() &&
                       plynth::planFingerprint(doc_.project()) != report_->planFingerprint;
    const QString outsideKind = outside ? outsideJob_->kind() : QString();
    if (outside) {
        const QString what = outsideKind == QStringLiteral("export") ? tr("Exporting from the command line")
                             : outsideKind == QStringLiteral("build") ? tr("Building from the command line")
                                                                       : tr("A build or an export runs from the command line");
        const QString message = outsideJob_->message();
        buildBar_->setProgress(message.isEmpty() ? what : tr("%1: %2").arg(what, message), outsideJob_->percent());
        buildBar_->setCancelTip(outsideKind == QStringLiteral("export")
                                    ? tr("Stop the export that runs from the command line. The last export stays as it was")
                                    : tr("Stop the work that runs from the command line. The last building stays as it was"));
    } else if (running) {
        buildBar_->setProgress(buildProgressText_, buildProgressPercent_);
        buildBar_->setCancelTip(tr("Stop the build. The last building stays as it was"));
    } else if (!report_) {
        buildBar_->setStatus(tr("Not built yet"));
    } else if (!report_->status.ok()) {
        buildBar_->setStatus(tr("The last build failed. See the Build report window."));
    } else if (stale) {
        buildBar_->setStatus(tr("The plan changed since this build"));
    } else {
        const QDateTime when =
            QDateTime::fromSecsSinceEpoch(report_->startedAt.time_since_epoch().count()).toLocalTime();
        buildBar_->setStatus(tr("Built at %1 in %2 s")
                                 .arg(when.toString(QStringLiteral("HH:mm")))
                                 .arg(report_->elapsedMs / 1000.0, 0, 'f', 1));
    }
    buildBar_->setProblems(report_ ? report_->errorCount() : 0, report_ ? report_->warningCount() : 0);
    QString reason;
    const bool blocked = buildBlocked(&reason);
    buildBar_->setBuildEnabled(!running, outside   ? tr("A build or an export runs from the command line. Build when it ends")
                                         : blocked ? tr("Open the setup check: %1").arg(reason)
                                                   : tr("Build the building in Blender (Ctrl+B)"));
    buildAction_->setEnabled(!running);
    cancelBuildAction_->setEnabled(running);
    const bool files = report_ && !report_->blendFile.empty();
    reportAction_->setEnabled(report_.has_value());
    buildFolderAction_->setEnabled(doc_.hasFolder());
    if (variationsAction_ != nullptr)
        variationsAction_->setEnabled(doc_.hasFolder() && !running);
    openBlenderAction_->setEnabled(files);
    save3dPictureAction_->setEnabled(files && !running);
    buildPanel_->setReport(report_ ? &*report_ : nullptr, stale);
    updateExportUi();
    refreshProblemMarkers();
}

// Gives the plan one marker for each problem of the last build that has a place on the shown floor.
void MainWindow::refreshProblemMarkers()
{
    QVector<ProblemMarker> markers;
    const std::optional<double> ppm = doc_.plan().scale.pixelsPerMetre;
    if (report_ && ppm.has_value() && focus_->floor() != nullptr) {
        const plynth::Floor& shown = *focus_->floor();
        for (const plynth::BuildProblem& problem : report_->problems) {
            if (!problem.floor.empty() && problem.floor != shown.id)
                continue;
            const std::optional<plynth::Vec2> at = plynth::problemPosition(doc_.plan(), problem);
            if (!at.has_value())
                continue;
            ProblemMarker marker;
            marker.position = QPointF(at->x * *ppm, at->y * *ppm);
            marker.error = problem.severity == plynth::BuildSeverity::Error;
            marker.tip = fromCore(problem.message);
            markers.append(marker);
        }
    }
    canvas_->setProblems(markers);
}

// Draws the building from above for the plan overlay, when the overlay is on and there is a built FBX.
void MainWindow::refreshBuilding3d()
{
    if (!overlay3dAction_->isChecked())
        return;
    std::error_code ignored;
    const std::filesystem::path fbx = preview_->fbxPath();
    const std::optional<double> ppm = doc_.plan().scale.pixelsPerMetre;
    if (fbx.empty() || !std::filesystem::exists(fbx, ignored) || !ppm.has_value()) {
        canvas_->setBuilding3d(QImage(), QRectF());
        return;
    }
    overlayRenderer_->render(QUrl::fromLocalFile(QString::fromStdWString(fbx.wstring())), doc_.plan(), *ppm, focus_->floorId());
}

// For checks only: shows the plan at 100 percent.
void MainWindow::setActualSize()
{
    canvas_->actualSize();
}

// For checks only: turns the overlay on or off.
void MainWindow::setOverlay3d(bool on)
{
    overlay3dAction_->setChecked(on);
}

// Opens the Plan page, selects the object of a problem and centres the plan on it. A floor id only switches to that
// floor, and a roof entry switches to its floor and selects its section.
void MainWindow::showProblem(const QString& objectId)
{
    showCentreView(false);
    const std::string key = objectId.toStdString();
    if (plynth::findFloor(doc_.plan(), key) != nullptr) {
        selection_->clear();
        showFloor(objectId);
        return;
    }
    if (const plynth::RoofSection* entry = plynth::findRoofSection(doc_.plan(), key)) {
        showFloor(fromCore(entry->floor));
        for (const plynth::RoofArea& area : plynth::roofAreasOf(doc_.plan(), entry->floor))
            if (area.section == key) {
                roofSectionsAction_->setChecked(true);
                selection_->set(SelectionKind::RoofSection, QStringLiteral("%1:%2").arg(fromCore(area.floor)).arg(area.index));
                break;
            }
        if (const std::optional<double> ppm = doc_.plan().scale.pixelsPerMetre)
            canvas_->centerOn(QPointF(entry->x * *ppm, entry->y * *ppm));
        return;
    }
    selectById(objectId);
    const std::optional<double> ppm = doc_.plan().scale.pixelsPerMetre;
    if (!ppm.has_value())
        return;
    plynth::BuildProblem problem;
    problem.object = objectId.toStdString();
    const std::optional<plynth::Vec2> at = plynth::problemPosition(doc_.plan(), problem);
    if (at.has_value())
        canvas_->centerOn(QPointF(at->x * *ppm, at->y * *ppm));
}

// Opens the build folder of the project in Explorer.
void MainWindow::openBuildFolder()
{
    if (!doc_.hasFolder())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(plynth::buildFolder(doc_.folder()).wstring())));
}

// Saves a picture of the last build as the 3D page shows it: its camera, roofs and cut. It goes through the same
// render as "plynth render", so both give the same picture for the same view.
void MainWindow::save3dPicture()
{
    if (!doc_.hasFolder())
        return;
    plynth::RenderOptions options;
    options.view = preview_->preset() == ModelView::Preset::Top ? plynth::RenderView::Top : plynth::RenderView::ThreeQuarter;
    options.showRoofs = showRoofs3d();
    if (cutAtFloor() && !focus_->floorId().isEmpty())
        options.cutFloor = focus_->floorId().toStdString();
    const std::filesystem::path suggested = doc_.folder() / std::string(plynth::kRendersFolderName) /
                                            (std::string(plynth::renderViewName(options.view)) + ".png");
    const QString file = QFileDialog::getSaveFileName(dialogParent(), tr("Save 3D picture"),
                                                      QString::fromStdWString(suggested.wstring()),
                                                      tr("PNG pictures (*.png)"));
    if (file.isEmpty())
        return;
    options.out = std::filesystem::path(file.toStdWString());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const plynth::RenderReport report = plynth::renderBuilding(doc_.folder(), doc_.project(), options, renderShot);
    QApplication::restoreOverrideCursor();
    if (!report.status.ok()) {
        showError(tr("Save 3D picture"), fromCore(report.status.message));
        return;
    }
    const QString shown = QString::fromStdWString(report.file.wstring());
    if (report.current)
        statusBar_->showMessage(tr("Saved the 3D picture %1").arg(shown), false);
    else
        statusBar_->showMessage(tr("Saved the 3D picture %1. The plan changed since the last build, so it shows the last build.").arg(shown), false);
}

// Opens building.blend in the Blender the settings choose, in a normal window.
void MainWindow::openInBlender()
{
    if (!report_ || report_->blendFile.empty())
        return;
    const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, host_->settings());
    if (!choice.status.ok()) {
        showError(tr("Open in Blender"), fromCore(choice.status.message));
        return;
    }
    QProcess::startDetached(QString::fromStdWString(choice.install.executable.wstring()),
                            {QString::fromStdWString(report_->blendFile.wstring())});
}

// For checks only: shows the Build report window.
void MainWindow::showBuildTab()
{
    openToolWindow(QStringLiteral("build"));
}

}
