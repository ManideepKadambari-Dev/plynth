#include "MainWindow.h"

#include "BuildBar.h"
#include "StatusBar.h"
#include "building/BuildController.h"
#include "building/BuildPanel.h"
#include "canvas/PlanCanvas.h"
#include "catalog/LibraryHost.h"
#include "panels/RightPanel.h"
#include "preview/OverlayRenderer.h"
#include "preview/GraphicsProbe.h"
#include "preview/PreviewView.h"

#include <plynth/core/Blender.h>
#include <plynth/core/SetupCheck.h>

#include <QAction>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
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
    if (builder_->running())
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
            QMessageBox::question(this, tr("Build"), tr("The project has to be saved before it can be built. Save it now?"),
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

// Shows the state of the last build and of the Build button in the bar, the menu and the Build tab.
void MainWindow::updateBuildUi()
{
    const bool running = builder_ != nullptr && builder_->running();
    const bool stale = report_ && report_->status.ok() && doc_.hasFolder() &&
                       plynth::planFingerprint(doc_.project()) != report_->planFingerprint;
    if (running) {
        buildBar_->setProgress(buildProgressText_, buildProgressPercent_);
    } else if (!report_) {
        buildBar_->setStatus(tr("Not built yet"));
    } else if (!report_->status.ok()) {
        buildBar_->setStatus(tr("The last build failed. See the Build tab."));
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
    buildBar_->setBuildEnabled(!running, blocked ? tr("Open the setup check: %1").arg(reason)
                                                 : tr("Build the building in Blender (Ctrl+B)"));
    buildAction_->setEnabled(!running);
    cancelBuildAction_->setEnabled(running);
    const bool files = report_ && !report_->blendFile.empty();
    reportAction_->setEnabled(report_.has_value());
    buildFolderAction_->setEnabled(doc_.hasFolder());
    openBlenderAction_->setEnabled(files);
    buildPanel_->setReport(report_ ? &*report_ : nullptr, stale);
    refreshProblemMarkers();
}

// Gives the plan one marker for each problem of the last build that has a place on the first floor.
void MainWindow::refreshProblemMarkers()
{
    QVector<ProblemMarker> markers;
    const std::optional<double> ppm = doc_.plan().scale.pixelsPerMetre;
    if (report_ && ppm.has_value() && !doc_.plan().floors.empty()) {
        const plynth::Floor& first = doc_.plan().floors.front();
        for (const plynth::BuildProblem& problem : report_->problems) {
            if (!problem.floor.empty() && problem.floor != first.id)
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
    overlayRenderer_->render(QUrl::fromLocalFile(QString::fromStdWString(fbx.wstring())), doc_.plan(), *ppm);
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

// Opens the Plan page, selects the object of a problem and centres the plan on it.
void MainWindow::showProblem(const QString& objectId)
{
    showCentreView(false);
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

// For checks only: shows the Build tab.
void MainWindow::showBuildTab()
{
    right_->showTab(RightPanel::Tab::Build);
}

}
