#include "MainWindow.h"

#include "BuildBar.h"
#include "StatusBar.h"
#include "building/BuildController.h"
#include "building/ExportController.h"
#include "catalog/LibraryHost.h"
#include "panels/ExportPanel.h"

#include <plynth/core/Build.h>
#include <plynth/core/Export.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/Exporter.h>

#include <QAction>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QUrl>

#include <filesystem>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// The sentence that says why an export cannot start, or an empty text when it can. offerBuild says a Build button
// helps.
QString MainWindow::exportNotice(bool* offerBuild) const
{
    if (offerBuild != nullptr)
        *offerBuild = false;
    if (doc_.project().settings.exportMode == plynth::ExportMode::Grouped && doc_.hasFolder()) {
        const plynth::ExportOptionsRead options = plynth::readExportOptions(doc_.folder());
        if (!options.status.ok())
            return fromCore(options.status.message);
    }
    std::error_code ignored;
    const bool built = doc_.hasFolder() &&
                       std::filesystem::exists(plynth::buildFolder(doc_.folder()) / "building.blend", ignored);
    if (!built) {
        if (offerBuild != nullptr)
            *offerBuild = true;
        return tr("Build the project first.");
    }
    QString reason;
    if (buildBlocked(&reason))
        return reason;
    return QString();
}

// Starts an export of the last build, or only its checks when write is false.
void MainWindow::startExport(bool write, bool force)
{
    if (builder_->running() || exporter_->running())
        return;
    const QString notice = exportNotice(nullptr);
    if (!notice.isEmpty()) {
        statusBar_->showMessage(notice);
        return;
    }
    plynth::ExportRequest request;
    request.projectFolder = doc_.folder();
    request.project = doc_.project();
    request.settings.write = write;
    request.settings.force = force;
    if (host_->hasLibrary())
        request.library = host_->paths();
    buildProgressText_ = tr("Starting");
    buildProgressPercent_ = 0;
    exporter_->start(std::move(request), host_->settings());
    updateBuildUi();
}

// Asks first, then exports with the failed checks of the last export left as they are.
void MainWindow::exportAnyway()
{
    if (!exportReport_ || exporter_->running() || builder_->running())
        return;
    QStringList failed;
    for (const plynth::QualityCheckResult& check : exportReport_->checks)
        if (check.status == plynth::QualityStatus::Fail)
            failed.append(fromCore(check.name));
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Export anyway"),
        tr("These checks failed: %1.\n\nThe export will go past them and the report will say so. Export anyway?")
            .arg(failed.join(QStringLiteral(", "))),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Yes)
        startExport(true, true);
}

// Takes the finished export: keeps its report and shows the Export window.
void MainWindow::exportFinished(const plynth::ExportReport& report)
{
    if (report.status.code == plynth::ErrorCode::Cancelled) {
        statusBar_->showMessage(tr("The export was cancelled. The last export stays as it was."), false);
    } else if (report.status.ok() || report.status.code == plynth::ErrorCode::QualityFailed) {
        exportReport_ = report;
        exportError_.clear();
        if (report.status.ok())
            statusBar_->showMessage(report.exported ? tr("Exported. The files are in the export folder.")
                                                    : (report.passed() ? tr("Every quality check passed. Nothing was written.")
                                                                       : tr("Some quality checks failed. Nothing was written.")),
                                    false);
        else
            statusBar_->showMessage((report.failCount() == 1 ? tr("Export blocked: 1 quality check failed.")
                                                          : tr("Export blocked: %1 quality checks failed.").arg(report.failCount())));
    } else {
        exportError_ = fromCore(report.status.message);
        statusBar_->showMessage(exportError_);
    }
    updateBuildUi();
    if (report.status.code != plynth::ErrorCode::Cancelled)
        openToolWindow(QStringLiteral("export"));
}

// Reads export/export_report.json of the open project, if there is one.
void MainWindow::loadExportReport()
{
    exportReport_.reset();
    exportError_.clear();
    if (doc_.hasFolder()) {
        const plynth::ExportReportRead read = plynth::readExportReport(doc_.folder());
        if (read.status.ok())
            exportReport_ = read.report;
    }
    updateExportUi();
}

// Shows the Export window and the Export items of the Build menu from the last export and the state of the work.
void MainWindow::updateExportUi()
{
    if (exportPanel_ == nullptr || exportAction_ == nullptr)
        return;
    const bool running = (builder_ != nullptr && builder_->running()) || (exporter_ != nullptr && exporter_->running());
    ExportPanelState state;
    state.running = running;
    state.notice = exportNotice(&state.offerBuild);
    state.canRun = state.notice.isEmpty();
    state.error = exportError_;
    std::error_code ignored;
    state.folderExists = doc_.hasFolder() && std::filesystem::exists(plynth::exportFolder(doc_.folder()), ignored);
    const bool blocked = exportReport_ && !exportReport_->exported && exportReport_->failCount() > 0;
    exportAction_->setEnabled(state.canRun && !running);
    checkExportAction_->setEnabled(state.canRun && !running);
    exportAnywayAction_->setEnabled(state.canRun && !running && blocked);
    exportFolderAction_->setEnabled(state.folderExists);
    state.grouped = doc_.project().settings.exportMode == plynth::ExportMode::Grouped;
    state.built = doc_.hasFolder() &&
                  std::filesystem::exists(plynth::buildFolder(doc_.folder()) / "building.blend", ignored);
    // The export records the build it came from. A later build has another plan fingerprint.
    state.reportOutOfDate = exportReport_ && exportReport_->status.ok() && report_ && report_->status.ok() &&
                            !exportReport_->buildFingerprint.empty() &&
                            exportReport_->buildFingerprint != report_->planFingerprint;
    state.project = &doc_.project();
    if (doc_.hasFolder()) {
        const plynth::ExportOptionsRead options = plynth::readExportOptions(doc_.folder());
        state.kitEnabled = options.status.ok() && options.options.kit.enabled;
        state.gameplayEnabled = options.status.ok() && options.options.gameplay.enabled;
        state.licensesEnabled = options.status.ok() && options.options.licenses.enabled;
        const std::string stem = plynth::exportFileStem(doc_.project().settings.name);
        state.gameplayFile = QString::fromStdWString(
            (plynth::exportFolder(doc_.folder()) / (stem + std::string(plynth::kGameplayFileSuffix))).make_preferred().wstring());
        state.licensesFile = QString::fromStdWString(
            (plynth::exportFolder(doc_.folder()) / (stem + std::string(plynth::kLicensesJsonSuffix))).make_preferred().wstring());
        if (!options.status.ok()) {
            state.optionsProblem = fromCore(options.status.message);
            state.optionsRepairable = options.status.code != plynth::ErrorCode::UnsupportedVersion;
        }
    }
    plynth::ExportGroupReview review;
    if (doc_.hasFolder()) {
        // Every mode needs the review, because the guard checks the kit switch and the repair with it too.
        state.projectFolder = doc_.folder();
        review = plynth::reviewExportGroups(doc_.folder(), doc_.project());
        state.review = &review;
    }
    connect(exportPanel_, &ExportPanel::refreshRequested, this, &MainWindow::updateExportUi, Qt::UniqueConnection);
    exportPanel_->setReport(exportReport_ ? &*exportReport_ : nullptr, state);
}

// Opens the export folder of the project in Explorer.
void MainWindow::openExportFolder()
{
    if (!doc_.hasFolder())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(plynth::exportFolder(doc_.folder()).wstring())));
}

// Stops the running build or export.
void MainWindow::cancelWork()
{
    builder_->cancel();
    exporter_->cancel();
}

// For checks only: shows the Export window.
void MainWindow::showExportTab()
{
    openToolWindow(QStringLiteral("export"));
}

}
