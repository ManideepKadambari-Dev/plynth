#include "MainWindow.h"

#include "BuildBar.h"
#include "building/BuildController.h"
#include "building/BuildPanel.h"
#include "building/ExportController.h"
#include "TitleBar.h"
#include "StatusBar.h"
#include "canvas/OverlayBuilder.h"
#include "canvas/PlanCanvas.h"
#include "ReadOnlyBanner.h"
#include "document/DocumentBridge.h"
#include "document/FloorFocus.h"
#include "FloorTabs.h"
#include "detection/DetectionSession.h"
#include "document/Selection.h"
#include "catalog/ImportQueue.h"
#include "catalog/LibraryHost.h"
#include "catalog/FabImportDialog.h"
#include "catalog/LibraryWatcher.h"
#include "catalog/StoreImports.h"
#include "panels/ImportQueueView.h"
#include "setup/SetupCheckDialog.h"
#include "dialogs/RoomTypesDialog.h"
#include "home/HomeScreen.h"
#include "home/ProjectThumbnail.h"
#include "materials/MaterialImportQueue.h"
#include "materials/MaterialImportScreen.h"
#include "materials/MaterialRefresher.h"
#include "panels/AssetsPanel.h"
#include "panels/ExportPanel.h"
#include "panels/IntegrationsPanel.h"
#include "panels/MaterialsPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/RightPanel.h"
#include "panels/ToolStrip.h"
#include "preview/GraphicsProbe.h"
#include "preview/OverlayRenderer.h"
#include "preview/PreviewView.h"
#include "tools/ToolController.h"
#include "document/ProjectWatcher.h"
#include "terminal/TerminalDock.h"
#include "wallview/WallView.h"
#include "theme/Theme.h"

#include <plynth/core/Items.h>
#include <plynth/core/PlanImage.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Version.h>
#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/SetupCheck.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/Integrations.h>
#include <plynth/core/commands/FloorCommands.h>

#include <QEventLoop>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <set>

#ifdef Q_OS_WIN
#include "FramelessWindow_win.h"
#endif

namespace plynth::app {

namespace {

// Turns a Qt path into a standard path without losing non English letters.
std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A text that changes whenever the image file at this path changes.
QString imageKey(const QString& path)
{
    const QFileInfo info(path);
    return info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.size()) + QLatin1Char('|') +
           QString::number(info.lastModified().toMSecsSinceEpoch());
}

}

// Builds the window, the title bar, the canvas, the menus and the autosave timer.
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent, Qt::Window | Qt::FramelessWindowHint), self_(plynth::currentProcess())
{
    setWindowIcon(QIcon(QStringLiteral(":/logo.png")));

    titleBar_ = new TitleBar(this);
    setMenuWidget(titleBar_);

    auto* central = new QWidget(this);
    auto* column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    focus_ = new FloorFocus(doc_, this);
    canvas_ = new PlanCanvas(central);
    canvas_->setAcceptDrops(true);
    canvas_->viewport()->setAcceptDrops(true);
    statusBar_ = new StatusBar(central);
    banner_ = new ReadOnlyBanner(central);
    banner_->hide();
    selection_ = new Selection(this);
    tools_ = new ToolController(doc_, canvas_, selection_, this, this);
    toolStrip_ = new ToolStrip(tools_, central);
    host_ = new LibraryHost(this);
    tools_->setLibrary(host_);
    tools_->setFocus(focus_);
    panel_ = new PropertiesPanel(doc_, *selection_, *tools_, central);
    assets_ = new AssetsPanel(*host_, central);
    materials_ = new MaterialsPanel(*host_, central);
    auto* work = new QWidget(central);
    auto* row = new QHBoxLayout(work);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(toolStrip_);
    splitter_ = new QSplitter(Qt::Vertical, work);
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(Theme::instance().unit());
    splitter_->setStyleSheet(QStringLiteral("QSplitter::handle { background: %1; }")
                                 .arg(Theme::instance().line().name()));
    canvas_->setParent(splitter_);
    splitter_->addWidget(canvas_);
    wallView_ = new WallView(doc_, *selection_, *tools_, splitter_);
    wallView_->hide();
    splitter_->addWidget(wallView_);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 2);
    auto* centre = new QWidget(work);
    auto* centreColumn = new QVBoxLayout(centre);
    centreColumn->setContentsMargins(0, 0, 0, 0);
    centreColumn->setSpacing(0);
    buildBar_ = new BuildBar(centre);
    centreColumn->addWidget(buildBar_);
    floorTabs_ = new FloorTabs(*focus_, centre);
    centreStack_ = new QStackedWidget(centre);
    centreStack_->addWidget(splitter_);
    preview_ = new PreviewView(centreStack_);
    centreStack_->addWidget(preview_);
    centreColumn->addWidget(centreStack_, 1);
    centreColumn->addWidget(floorTabs_);
    row->addWidget(centre, 1);
    buildPanel_ = new BuildPanel(work);
    exportPanel_ = new ExportPanel(work);
    integrations_ = new IntegrationsPanel(*host_, work);
    right_ = new RightPanel(panel_, assets_, materials_, buildPanel_, exportPanel_, integrations_, work);
    row->addWidget(right_);
    work_ = work;
    pages_ = new QStackedWidget(central);
    pages_->addWidget(work);
    home_ = new HomeScreen(*host_, pages_);
    pages_->addWidget(home_);
    pages_->setCurrentWidget(work);
    column->addWidget(banner_);
    column->addWidget(pages_, 1);
    column->addWidget(statusBar_);
    setCentralWidget(central);
    setAcceptDrops(true);
    terminal_ = new TerminalDock(work, centreStack_, this);
    terminal_->setWorkingFolder(host_->projectsFolder());
    lastFolder_ = host_->projectsFolder();

    overlayRenderer_ = new OverlayRenderer(this);
    connect(overlayRenderer_, &OverlayRenderer::ready, this, [this](const QImage& image, const QRectF& rect) {
        canvas_->setBuilding3d(image, rect);
        canvas_->setBuilding3dShown(overlay3dAction_->isChecked());
    });
    builder_ = new BuildController(this);
    buildUiTimer_ = new QTimer(this);
    buildUiTimer_->setSingleShot(true);
    buildUiTimer_->setInterval(300);
    connect(buildUiTimer_, &QTimer::timeout, this, &MainWindow::updateBuildUi);
    connect(builder_, &BuildController::progress, this, [this](const QString& text, int percent) {
        buildProgressText_ = text;
        buildProgressPercent_ = percent;
        updateBuildUi();
    });
    connect(builder_, &BuildController::finished, this, &MainWindow::buildFinished);
    connect(focus_, &FloorFocus::changed, this, &MainWindow::floorShown);
    connect(floorTabs_, &FloorTabs::addAboveRequested, this, [this]() { addFloor(true, QString()); });
    connect(floorTabs_, &FloorTabs::addBelowRequested, this, [this](const QString& id) { addFloor(false, id); });
    connect(floorTabs_, &FloorTabs::renameRequested, this, &MainWindow::renameFloor);
    connect(floorTabs_, &FloorTabs::removeRequested, this, &MainWindow::removeFloor);
    connect(tools_, &ToolController::alignImageChanged, this, [this]() { updateAlignAction(); });
    connect(panel_, &PropertiesPanel::planImageRequested, this, &MainWindow::setImageDialog);
    exporter_ = new ExportController(this);
    connect(exporter_, &ExportController::progress, this, [this](const QString& text, int percent) {
        buildProgressText_ = text;
        buildProgressPercent_ = percent;
        updateBuildUi();
    });
    connect(exporter_, &ExportController::finished, this, &MainWindow::exportFinished);
    connect(buildBar_, &BuildBar::buildRequested, this, &MainWindow::startBuild);
    connect(buildBar_, &BuildBar::cancelRequested, this, &MainWindow::cancelWork);
    connect(buildBar_, &BuildBar::problemsRequested, this, [this]() { right_->showTab(RightPanel::Tab::Build); });
    connect(buildPanel_, &BuildPanel::problemActivated, this, &MainWindow::showProblem);
    connect(buildPanel_, &BuildPanel::openFolderRequested, this, &MainWindow::openBuildFolder);
    connect(buildPanel_, &BuildPanel::openBlenderRequested, this, &MainWindow::openInBlender);
    connect(exportPanel_, &ExportPanel::exportRequested, this, [this]() { startExport(true, false); });
    connect(exportPanel_, &ExportPanel::checkRequested, this, [this]() { startExport(false, false); });
    connect(exportPanel_, &ExportPanel::exportAnywayRequested, this, &MainWindow::exportAnyway);
    connect(exportPanel_, &ExportPanel::buildRequested, this, &MainWindow::startBuild);
    connect(exportPanel_, &ExportPanel::openFolderRequested, this, &MainWindow::openExportFolder);
    connect(exportPanel_, &ExportPanel::findingActivated, this, &MainWindow::showProblem);
    connect(buildBar_, &BuildBar::viewRequested, this,
            [this](BuildBar::View view) { showCentreView(view == BuildBar::View::Scene3d); });
    connect(home_, &HomeScreen::newProjectRequested, this, &MainWindow::newProject);
    connect(home_, &HomeScreen::newFromImageRequested, this, &MainWindow::newProjectFromImage);
    connect(home_, &HomeScreen::openProjectRequested, this, &MainWindow::openProjectDialog);
    connect(home_, &HomeScreen::openRecentRequested, this, &MainWindow::openRecent);
    connect(home_, &HomeScreen::openSampleRequested, this, [this](const QString& file) {
        if (!confirmDiscardChanges())
            return;
        QString error;
        if (!newFromImage(file, &error)) {
            showError(tr("Cannot open image"), error);
            return;
        }
        terminal_->shutdown();
    });
    connect(home_, &HomeScreen::setupRequested, this, &MainWindow::openSetupCheck);
    connect(home_, &HomeScreen::backRequested, this, &MainWindow::showWork);
    connect(canvas_, &PlanCanvas::zoomChanged, this, [this](double zoom) {
        if (canvas_->hasImage())
            statusBar_->setZoom(zoom);
        else
            statusBar_->clearZoom();
    });

    connect(canvas_, &PlanCanvas::cursorMoved, this, [this](const QPointF& scene) { updateCursorText(&scene); });
    connect(canvas_, &PlanCanvas::cursorLeft, this, [this]() { updateCursorText(nullptr); });
    connect(selection_, &Selection::changed, this, &MainWindow::refreshSelection);
    connect(panel_, &PropertiesPanel::deleteRequested, this, &MainWindow::deleteSelection);
    connect(tools_, &ToolController::message, statusBar_, [this](const QString& text) { statusBar_->showMessage(text); });
    connect(tools_, &ToolController::notice, statusBar_, [this](const QString& text) { statusBar_->showMessage(text, false); });
    connect(&tools_->detection(), &DetectionSession::changed, this, &MainWindow::updateToolActions);
    connect(tools_, &ToolController::hintTextChanged, statusBar_, &StatusBar::setHint);
    connect(tools_, &ToolController::gridChanged, this, [this]() { canvas_->setGridStep(tools_->gridStepPixels()); });
    connect(tools_, &ToolController::activeChanged, this, &MainWindow::updateToolActions);
    connect(tools_, &ToolController::availabilityChanged, this, &MainWindow::updateToolActions);
    statusBar_->setHint(tools_->hint());

    buildMenus();
    buildCatalogMenu();
    updateBuildUi();
    connect(host_, &LibraryHost::roomTypesChanged, this, &MainWindow::roomTypesChanged);
    connect(assets_, &AssetsPanel::message, statusBar_, [this](const QString& text) { statusBar_->showMessage(text); });
    if (!host_->shapeNotice().isEmpty())
        statusBar_->showMessage(host_->shapeNotice());

    queue_ = new ImportQueue(*host_, this);
    queueView_ = new ImportQueueView(*queue_, assets_);
    assets_->setQueueView(queueView_);
    libraryWatcher_ = new LibraryWatcher(*host_, *queue_, this);
    materialRefresher_ = new MaterialRefresher(*host_, this);
    libraryWatcher_->setMaterialRefresher(materialRefresher_);
    materialQueue_ = new MaterialImportQueue(*host_, this);
    libraryWatcher_->setMaterialQueue(materialQueue_);
    materials_->setQueueView(new ImportQueueView(*materialQueue_, materials_));
    storeImports_ = new StoreImports(*host_, *queue_, *materialQueue_, this, this);
    storeImports_->setOwnFiles([this](bool material) {
        if (material)
            showMaterialImport();
        else
            importDialog();
    });
    storeImports_->setOwnPaths([this](const QStringList& paths, bool material) {
        if (!material) {
            importPaths(paths);
            return;
        }
        materialImport_->addSources(paths);
        showMaterialImport();
    });
    integrations_->setActions(storeImports_);
    connect(queueView_, &ImportQueueView::pickModelRequested, storeImports_, &StoreImports::pickModel);
    connect(storeImports_, &StoreImports::notice, statusBar_, [this](const QString& text) { statusBar_->showMessage(text); });
    connect(host_, &LibraryHost::libraryChanged, integrations_, &IntegrationsPanel::refresh);
    materialImport_ = new MaterialImportScreen(*host_, *materialQueue_, pages_);
    pages_->addWidget(materialImport_);
    connect(materialImport_, &MaterialImportScreen::backRequested, this, &MainWindow::showWork);
    connect(host_, &LibraryHost::materialsChanged, materialImport_, &MaterialImportScreen::refreshLists);
    connect(host_, &LibraryHost::libraryChanged, materialImport_, &MaterialImportScreen::refreshLists);
    connect(materialQueue_, &MaterialImportQueue::finished, this, &MainWindow::materialImportFinished);
    connect(materials_, &MaterialsPanel::importRequested, this, &MainWindow::showMaterialImport);
    connect(materials_, &MaterialsPanel::samplesRequested, this, &MainWindow::addSampleMaterials);
    connect(importMaterialsAction_, &QAction::triggered, this, &MainWindow::showMaterialImport);
    connect(addSamplesAction_, &QAction::triggered, this, &MainWindow::addSampleMaterials);
    connect(assets_, &AssetsPanel::addMaterialRequested, this, &MainWindow::addMaterialFromAsset);
    connect(materialRefresher_, &MaterialRefresher::busyChanged, this,
            [this]() { materials_->setBusyMaterials(materialRefresher_->busyIds()); });
    connect(materialRefresher_, &MaterialRefresher::finished, this,
            [this](const QString&, bool ok, const QString& message) {
                host_->materialsEdited();
                if (!ok)
                    statusBar_->showMessage(tr("The material render failed. %1").arg(message));
            });
    connect(materials_, &MaterialsPanel::retryRequested, materialRefresher_, &MaterialRefresher::enqueue);
    connect(materials_, &MaterialsPanel::rescanRequested, libraryWatcher_, &LibraryWatcher::rescanMaterialsNow);
    connect(materials_, &MaterialsPanel::setupRequested, this, &MainWindow::openSetupCheck);
    connect(materials_, &MaterialsPanel::message, statusBar_, [this](const QString& text) { statusBar_->showMessage(text); });
    connect(rescanMaterialsAction_, &QAction::triggered, libraryWatcher_, &LibraryWatcher::rescanMaterialsNow);
    connect(queue_, &ImportQueue::finished, this, &MainWindow::queueFinished);
    connect(queue_, &ImportQueue::itemsChanged, this, [this]() { assets_->setBusyAssets(queue_->refreshingAssets()); });
    connect(libraryWatcher_, &LibraryWatcher::message, statusBar_,
            [this](const QString& text) { statusBar_->showMessage(text, false); });
    connect(assets_, &AssetsPanel::importRequested, this, &MainWindow::importDialog);
    connect(assets_, &AssetsPanel::rescanRequested, libraryWatcher_, &LibraryWatcher::rescanNow);
    connect(assets_, &AssetsPanel::setupRequested, this, &MainWindow::openSetupCheck);
    connect(assets_, &AssetsPanel::filesDropped, this, &MainWindow::importPaths);
    connect(assets_, &AssetsPanel::retryRequested, this, [this](const QString& id) {
        const std::optional<plynth::AssetRecord> record =
            host_->catalog() ? host_->catalog()->asset(id.toStdString()) : std::nullopt;
        queue_->enqueueRefresh(id, record ? fromCore(record->name) : id);
    });
    connect(importAction_, &QAction::triggered, this, &MainWindow::importDialog);
    connect(rescanAction_, &QAction::triggered, libraryWatcher_, &LibraryWatcher::rescanNow);
    const auto syncImportState = [this]() {
        const bool open = host_->hasLibrary();
        assets_->setImportEnabled(open, open ? tr("Import model files into the library (Ctrl+I)")
                                             : tr("Set a library folder first"));
        importAction_->setEnabled(open);
        rescanAction_->setEnabled(open);
        rescanMaterialsAction_->setEnabled(open);
        materials_->setRenderEnabled(open);
        const bool materialsOpen = open && host_->materialCatalog() != nullptr;
        materials_->setImportEnabled(materialsOpen, materialsOpen ? tr("Open the import screen for materials")
                                                                  : tr("Set a library folder first"));
        importMaterialsAction_->setEnabled(materialsOpen);
        importFabAction_->setEnabled(open);
        addSamplesAction_->setEnabled(materialsOpen);
        openGeneratorsAction_->setEnabled(open);
        reloadGeneratorsAction_->setEnabled(open);
        checkAddonsAction_->setEnabled(open);
        addSampleGeneratorsAction_->setEnabled(open);
        addShapesAction_->setEnabled(open);
        materialImport_->setLibraryOpen(materialsOpen);
        assets_->setMaterialImportEnabled(materialsOpen);
    };
    connect(host_, &LibraryHost::libraryChanged, this, syncImportState);
    syncImportState();
    connect(host_, &LibraryHost::libraryChanged, this, &MainWindow::refreshOverlay);
    connect(host_, &LibraryHost::libraryChanged, this, [this]() {
        tools_->refreshAvailability();
        updateToolActions();
    });
    connect(canvas_, &PlanCanvas::assetDropped, this, [this](const QString& id, const QPointF& scene, bool free) {
        const std::optional<double> ppm = doc_.plan().scale.pixelsPerMetre;
        if (!ppm.has_value()) {
            statusBar_->showMessage(tr("Set the scale first."));
            return;
        }
        tools_->placeAsset(id, plynth::Vec2{scene.x() / *ppm, scene.y() / *ppm}, free);
    });
    qApp->installEventFilter(this);
    connect(host_, &LibraryHost::assetsChanged, this, &MainWindow::refreshOverlay);
    connect(host_, &LibraryHost::generatorsChanged, this, &MainWindow::refreshOverlay);
    connect(host_, &LibraryHost::libraryChanged, wallView_, &WallView::refresh);
    connect(host_, &LibraryHost::assetsChanged, wallView_, &WallView::refresh);

    connect(titleBar_, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &TitleBar::maximizeToggleRequested, this, &MainWindow::toggleMaximized);
    connect(titleBar_, &TitleBar::closeRequested, this, &QWidget::close);

    bridge_ = std::make_unique<DocumentBridge>(doc_);
    connect(bridge_.get(), &DocumentBridge::changed, this,
            [this](unsigned changes, const QString&) { onDocumentChanged(changes); });

    watcher_ = new ProjectWatcher(this);
    connect(watcher_, &ProjectWatcher::checkRequested, this, &MainWindow::handleCheck);
    connect(banner_, &ReadOnlyBanner::unlockRequested, this, &MainWindow::unlockNow);

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(plynth::kAutosaveIntervalSeconds * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::runAutosave);
    autosaveTimer_->start();

    refreshFloorTabs();
    refreshImage();
    refreshOverlay();
    updateTitle();
    updateActions();
}

// Removes the app state file so the CLI knows the project is no longer open.
MainWindow::~MainWindow()
{
    removeAppState();
}

// The name shown in the title bar.
QString MainWindow::projectName() const
{
    return fromCore(doc_.project().settings.name);
}

// Screenshot mode never writes into a project folder, so the timer stops.
void MainWindow::setScreenshotMode(bool on)
{
    screenshotMode_ = on;
    if (on) {
        autosaveTimer_->stop();
        watcher_->setActive(false);
        libraryWatcher_->setActive(false);
    }
}

// Builds the File, Edit and View menu actions with their tooltips and tour ids.
void MainWindow::buildMenus()
{
    QMenu* file = titleBar_->fileMenu();
    newAction_ = addMenuAction(file, tr("&New project"), tr("Start a new empty project (Ctrl+N)"),
                               QStringLiteral("file-new"), {QKeySequence(Qt::CTRL | Qt::Key_N)});
    connect(newAction_, &QAction::triggered, this, &MainWindow::newProject);
    newFromImageAction_ = addMenuAction(file, tr("New project from &image..."),
                                        tr("Start a new project from a plan image (Ctrl+Shift+N)"),
                                        QStringLiteral("file-new-from-image"),
                                        {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)});
    connect(newFromImageAction_, &QAction::triggered, this, &MainWindow::newProjectFromImage);
    homeAction_ = addMenuAction(file, tr("&Home"), tr("Show the home screen with the recent projects"),
                                QStringLiteral("file-home"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H)});
    connect(homeAction_, &QAction::triggered, this, &MainWindow::showHome);
    openAction_ = addMenuAction(file, tr("&Open project..."), tr("Open a project folder (Ctrl+O)"),
                                QStringLiteral("file-open-project"), {QKeySequence(Qt::CTRL | Qt::Key_O)});
    connect(openAction_, &QAction::triggered, this, &MainWindow::openProjectDialog);
    closeAction_ = addMenuAction(file, tr("&Close project"), tr("Close the project and go to the home screen (Ctrl+W)"),
                                 QStringLiteral("file-close"), {QKeySequence(Qt::CTRL | Qt::Key_W)});
    connect(closeAction_, &QAction::triggered, this, &MainWindow::closeProject);
    setImageAction_ = addMenuAction(file, tr("Set plan &image..."),
                                    tr("Set the plan image of the floor (Ctrl+Shift+O)"),
                                    QStringLiteral("file-set-image"),
                                    {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)});
    connect(setImageAction_, &QAction::triggered, this, &MainWindow::setImageDialog);
    file->addSeparator();
    saveAction_ = addMenuAction(file, tr("&Save"), tr("Save the project (Ctrl+S)"),
                                QStringLiteral("file-save"), {QKeySequence(Qt::CTRL | Qt::Key_S)});
    connect(saveAction_, &QAction::triggered, this, [this]() { saveDocument(); });
    saveAsAction_ = addMenuAction(file, tr("Save &as..."), tr("Save the project in another folder (Ctrl+Shift+S)"),
                                  QStringLiteral("file-save-as"),
                                  {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)});
    connect(saveAsAction_, &QAction::triggered, this, [this]() { saveDocumentAs(); });
    file->addSeparator();
    QAction* exitAction = addMenuAction(file, tr("E&xit"), tr("Exit Plynth (Alt+F4)"),
                                        QStringLiteral("file-exit"), {QKeySequence(Qt::ALT | Qt::Key_F4)});
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* edit = titleBar_->editMenu();
    undoAction_ = addMenuAction(edit, tr("&Undo"), tr("Undo the last change (Ctrl+Z)"),
                                QStringLiteral("edit-undo"), {QKeySequence(Qt::CTRL | Qt::Key_Z)});
    connect(undoAction_, &QAction::triggered, this, [this]() { doc_.undo(); });
    redoAction_ = addMenuAction(edit, tr("&Redo"), tr("Redo the change that was undone (Ctrl+Y)"),
                                QStringLiteral("edit-redo"),
                                {QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(redoAction_, &QAction::triggered, this, [this]() {
        const plynth::Status status = doc_.redo();
        if (!status.ok())
            showError(tr("Cannot redo"), fromCore(status.message));
    });

    edit->addSeparator();
    const std::array<ToolId, kToolCount>& toolIds = kStripTools;
    const std::array<Qt::Key, kToolCount> toolKeys{Qt::Key_V, Qt::Key_S, Qt::Key_W, Qt::Key_O,
                                                   Qt::Key_R, Qt::Key_T, Qt::Key_P, Qt::Key_D};
    for (size_t i = 0; i < toolIds.size(); ++i) {
        const ToolId id = toolIds[i];
        QAction* action = addMenuAction(edit, tr("%1 tool").arg(ToolStrip::toolName(id)),
                                        tr("%1 tool (%2)").arg(ToolStrip::toolName(id), ToolStrip::toolKey(id)),
                                        QStringLiteral("tool-") + ToolStrip::toolName(id).toLower(),
                                        {QKeySequence(toolKeys[i])});
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, id]() {
            tools_->setActive(id);
            updateToolActions();
        });
        toolActions_[static_cast<size_t>(id)] = action;
    }
    turnLeftAction_ = addMenuAction(edit, tr("Turn item left"),
                                    tr("Turn the selected item 90 degrees counterclockwise ([)"),
                                    QStringLiteral("item-turn-left"), {QKeySequence(Qt::Key_BracketLeft)});
    connect(turnLeftAction_, &QAction::triggered, this, [this]() { tools_->turnSelectedItem(-plynth::kItemQuarterTurn); });
    turnRightAction_ = addMenuAction(edit, tr("Turn item right"),
                                     tr("Turn the selected item 90 degrees clockwise (])"),
                                     QStringLiteral("item-turn-right"), {QKeySequence(Qt::Key_BracketRight)});
    connect(turnRightAction_, &QAction::triggered, this, [this]() { tools_->turnSelectedItem(plynth::kItemQuarterTurn); });
    mountAction_ = addMenuAction(edit, tr("Mount item on wall"), tr("Hang the selected item flat on the nearest wall"),
                                 QStringLiteral("item-mount"), {});
    connect(mountAction_, &QAction::triggered, this, [this]() { panel_->mountItem(); });
    takeOffAction_ = addMenuAction(edit, tr("Take item off wall"), tr("Take the selected item off its wall"),
                                   QStringLiteral("item-take-off"), {});
    connect(takeOffAction_, &QAction::triggered, this, [this]() { panel_->takeOffItem(); });
    swapAction_ = addMenuAction(edit, tr("Swap item asset..."), tr("Pick another asset for the selected item"),
                                QStringLiteral("item-swap"), {});
    connect(swapAction_, &QAction::triggered, this, [this]() { panel_->swapItemAsset(); });
    updateSizeAction_ = addMenuAction(edit, tr("Update item size"), tr("Use the size the library has now for the selected item"),
                                      QStringLiteral("item-update-size"), {});
    connect(updateSizeAction_, &QAction::triggered, this, [this]() { panel_->updateItemSize(); });
    acceptDetectionAction_ = addMenuAction(edit, tr("Accept detection"),
                                           tr("Add the detected walls, openings and rooms to the plan (Enter)"),
                                           QStringLiteral("detect-accept"),
                                           {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    connect(acceptDetectionAction_, &QAction::triggered, this, [this]() { tools_->detection().accept(); });
    updateToolActions();
    edit->addSeparator();
    deleteAction_ = addMenuAction(edit, tr("&Delete"),
                                  tr("Delete the selected item and what depends on it (Delete)"),
                                  QStringLiteral("edit-delete"),
                                  {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteSelection);
    updateDeleteAction();

    QList<QKeySequence> zoomOutKeys{QKeySequence(Qt::CTRL | Qt::Key_Minus)};
    for (const QKeySequence& key : QKeySequence::keyBindings(QKeySequence::ZoomOut))
        if (!zoomOutKeys.contains(key))
            zoomOutKeys.append(key);
    addViewAction(tr("Zoom &in"), tr("Zoom in (Ctrl+=)"), QStringLiteral("view-zoom-in"),
                  {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)},
                  &PlanCanvas::zoomIn);
    addViewAction(tr("Zoom &out"), tr("Zoom out (Ctrl+-)"), QStringLiteral("view-zoom-out"),
                  zoomOutKeys, &PlanCanvas::zoomOut);
    fitAction_ = addViewAction(tr("&Fit to window"), tr("Fit the whole plan, or the building in 3D, in the window (Ctrl+0)"),
                               QStringLiteral("view-fit"), {QKeySequence(Qt::CTRL | Qt::Key_0)},
                               &PlanCanvas::fitToWindow);
    disconnect(fitAction_, &QAction::triggered, canvas_, nullptr);
    connect(fitAction_, &QAction::triggered, this, [this]() {
        if (centreStack_->currentWidget() == preview_)
            preview_->fit();
        else
            canvas_->fitToWindow();
    });
    addViewAction(tr("&Actual size (100%)"), tr("Show the plan at 100 percent (Ctrl+1)"),
                  QStringLiteral("view-actual-size"), {QKeySequence(Qt::CTRL | Qt::Key_1)},
                  &PlanCanvas::actualSize);
    updateViewActions();
    titleBar_->viewMenu()->addSeparator();
    view3dAction_ = addMenuAction(titleBar_->viewMenu(), tr("&3D view"),
                                  tr("Show the building in 3D instead of the plan (3)"),
                                  QStringLiteral("view-3d"), {QKeySequence(Qt::Key_3)});
    view3dAction_->setCheckable(true);
    connect(view3dAction_, &QAction::triggered, this, [this](bool on) { showCentreView(on); });
    overlay3dAction_ = addMenuAction(titleBar_->viewMenu(), tr("3D &overlay on plan"),
                                     tr("Show the top view of the built building over the plan (Alt+3)"),
                                     QStringLiteral("view-overlay-3d"), {QKeySequence(Qt::ALT | Qt::Key_3)});
    overlay3dAction_->setCheckable(true);
    connect(overlay3dAction_, &QAction::toggled, this, [this](bool on) {
        canvas_->setBuilding3dShown(on);
        if (on)
            refreshBuilding3d();
    });
    problemsOnPlanAction_ = addMenuAction(titleBar_->viewMenu(), tr("Build &problems on plan"),
                                          tr("Show the problems of the last build as amber triangles on the plan"),
                                          QStringLiteral("view-build-problems"), {});
    problemsOnPlanAction_->setCheckable(true);
    problemsOnPlanAction_->setChecked(true);
    connect(problemsOnPlanAction_, &QAction::toggled, canvas_, &PlanCanvas::setProblemsShown);
    roofSectionsAction_ = addMenuAction(titleBar_->viewMenu(), tr("&Roof sections"),
                                        tr("Show the roof sections of the shown floor on the plan, with their type. Click one to edit it (Alt+R)"),
                                        QStringLiteral("view-roof-sections"), {QKeySequence(Qt::ALT | Qt::Key_R)});
    roofSectionsAction_->setCheckable(true);
    connect(roofSectionsAction_, &QAction::toggled, canvas_, &PlanCanvas::setRoofSectionsShown);
    connect(canvas_, &PlanCanvas::roofSectionClicked, this,
            [this](const QString& id) { selection_->set(SelectionKind::RoofSection, id); });
    showRoofs3dAction_ = addMenuAction(titleBar_->viewMenu(), tr("Show roofs in &3D"),
                                       tr("Show the roofs in the 3D view. Turn off to see the rooms under them"),
                                       QStringLiteral("view-show-roofs-3d"), {});
    showRoofs3dAction_->setCheckable(true);
    showRoofs3dAction_->setChecked(true);
    connect(showRoofs3dAction_, &QAction::toggled, this, [this]() { applyPreviewRoofOptions(); });
    cutAtFloorAction_ = addMenuAction(titleBar_->viewMenu(), tr("&Cut 3D at the shown floor"),
                                      tr("Show the floors up to the shown one in 3D, without the shown floor's roof"),
                                      QStringLiteral("view-cut-at-floor"), {});
    cutAtFloorAction_->setCheckable(true);
    connect(cutAtFloorAction_, &QAction::toggled, this, [this]() { applyPreviewRoofOptions(); });
    titleBar_->viewMenu()->addSeparator();
    snapGridAction_ = addMenuAction(titleBar_->viewMenu(), tr("Snap to &grid"),
                                    tr("Snap new and moved points to the grid (G)"), QStringLiteral("view-snap-grid"),
                                    {QKeySequence(Qt::Key_G)});
    snapGridAction_->setCheckable(true);
    snapGridAction_->setChecked(tools_->snapToGrid());
    connect(snapGridAction_, &QAction::toggled, this, [this](bool on) { tools_->setSnapToGrid(on); });
    titleBar_->viewMenu()->addSeparator();
    alignImageAction_ = addMenuAction(titleBar_->viewMenu(), tr("&Align plan image"),
                                      tr("Drag the plan image of the shown floor to line it up with the floor below, or nudge it with the arrow keys"),
                                      QStringLiteral("view-align-image"), {});
    alignImageAction_->setCheckable(true);
    connect(alignImageAction_, &QAction::triggered, this, [this](bool on) {
        tools_->setAlignImage(on);
        updateAlignAction();
    });
    floorUpAction_ = addMenuAction(titleBar_->viewMenu(), tr("Floor &above"), tr("Show the next floor up (Ctrl+Page Up)"),
                                   QStringLiteral("view-floor-up"), {QKeySequence(Qt::CTRL | Qt::Key_PageUp)});
    connect(floorUpAction_, &QAction::triggered, this, [this]() { stepFloor(1); });
    floorDownAction_ = addMenuAction(titleBar_->viewMenu(), tr("Floor b&elow"), tr("Show the next floor down (Ctrl+Page Down)"),
                                     QStringLiteral("view-floor-down"), {QKeySequence(Qt::CTRL | Qt::Key_PageDown)});
    connect(floorDownAction_, &QAction::triggered, this, [this]() { stepFloor(-1); });
    updateAlignAction();
    titleBar_->viewMenu()->addSeparator();
    wallViewAction_ = addMenuAction(titleBar_->viewMenu(), tr("&Wall view"),
                                    tr("Show the face of the selected wall under the plan (E)"),
                                    QStringLiteral("view-wall-view"), {QKeySequence(Qt::Key_E)});
    wallViewAction_->setCheckable(true);
    wallViewAction_->setChecked(true);
    connect(wallViewAction_, &QAction::toggled, this, &MainWindow::updateWallView);
    flipWallViewAction_ = addMenuAction(titleBar_->viewMenu(), tr("Fl&ip wall view"),
                                        tr("See the other side of the wall (F)"),
                                        QStringLiteral("wall-view-flip"), {QKeySequence(Qt::Key_F)});
    flipWallViewAction_->setEnabled(false);
    connect(flipWallViewAction_, &QAction::triggered, wallView_, &WallView::flip);
    titleBar_->viewMenu()->addSeparator();
    QAction* terminalAction = addMenuAction(titleBar_->viewMenu(), tr("&Terminal"),
                                            tr("Show or hide the terminal (Ctrl+`)"), QStringLiteral("view-terminal"),
                                            {QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft)});
    connect(terminalAction, &QAction::triggered, terminal_, &TerminalDock::toggle);

    QMenu* build = titleBar_->buildMenu();
    buildAction_ = addMenuAction(build, tr("&Build"), tr("Build the building in Blender (Ctrl+B)"),
                                 QStringLiteral("build-run"), {QKeySequence(Qt::CTRL | Qt::Key_B)});
    connect(buildAction_, &QAction::triggered, this, &MainWindow::startBuild);
    cancelBuildAction_ = addMenuAction(build, tr("&Cancel build"), tr("Stop the running build"),
                                       QStringLiteral("build-cancel-action"), {});
    connect(cancelBuildAction_, &QAction::triggered, this, &MainWindow::cancelWork);
    build->addSeparator();
    reportAction_ = addMenuAction(build, tr("Show build &report"), tr("Show the last build in the Build tab"),
                                  QStringLiteral("build-report"), {});
    connect(reportAction_, &QAction::triggered, this, [this]() { right_->showTab(RightPanel::Tab::Build); });
    buildFolderAction_ = addMenuAction(build, tr("&Open build folder"), tr("Open the build folder of the project in Explorer"),
                                       QStringLiteral("build-open-folder-action"), {});
    connect(buildFolderAction_, &QAction::triggered, this, &MainWindow::openBuildFolder);
    openBlenderAction_ = addMenuAction(build, tr("Open building.&blend in Blender"),
                                       tr("Open the built Blender file in Blender"),
                                       QStringLiteral("build-open-blender-action"), {});
    connect(openBlenderAction_, &QAction::triggered, this, &MainWindow::openInBlender);
    build->addSeparator();
    exportAction_ = addMenuAction(build, tr("&Export"), tr("Write the FBX and textures for Unreal (Ctrl+E)"),
                                  QStringLiteral("export-run-action"), {QKeySequence(Qt::CTRL | Qt::Key_E)});
    connect(exportAction_, &QAction::triggered, this, [this]() { startExport(true, false); });
    checkExportAction_ = addMenuAction(build, tr("C&heck export"), tr("Run the quality checks and write nothing (Ctrl+Shift+E)"),
                                       QStringLiteral("export-check-action"),
                                       {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)});
    connect(checkExportAction_, &QAction::triggered, this, [this]() { startExport(false, false); });
    exportAnywayAction_ = addMenuAction(build, tr("Export &anyway..."),
                                        tr("Export with the failed checks of the last export left as they are. Asks first"),
                                        QStringLiteral("export-anyway-action"), {});
    connect(exportAnywayAction_, &QAction::triggered, this, &MainWindow::exportAnyway);
    exportFolderAction_ = addMenuAction(build, tr("Open e&xport folder"), tr("Open the export folder of the project in Explorer"),
                                        QStringLiteral("export-open-folder-action"), {});
    connect(exportFolderAction_, &QAction::triggered, this, &MainWindow::openExportFolder);

    QAction* setupAction = addMenuAction(titleBar_->helpMenu(), tr("&Setup check"),
                                         tr("Check that Blender, the library and the projects folder are ready"),
                                         QStringLiteral("help-setup-check"), {});
    connect(setupAction, &QAction::triggered, this, &MainWindow::openSetupCheck);
    auto* aboutAction = titleBar_->helpMenu()->addAction(tr("&About Plynth"));
    aboutAction->setToolTip(tr("Show the Plynth version"));
    aboutAction->setProperty("tourId", QStringLiteral("help-about"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

// Builds the Catalog menu. Import and Rescan are connected where the import queue is made.
void MainWindow::buildCatalogMenu()
{
    QMenu* menu = titleBar_->catalogMenu();
    QAction* show = addMenuAction(menu, tr("&Assets catalog"), tr("Show the Assets tab (Ctrl+Shift+A)"),
                                  QStringLiteral("catalog-show"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A)});
    connect(show, &QAction::triggered, this, [this]() { right_->showTab(RightPanel::Tab::Assets); });
    importAction_ = addMenuAction(menu, tr("&Import asset..."), tr("Import model files into the library (Ctrl+I)"),
                                  QStringLiteral("catalog-import-action"), {QKeySequence(Qt::CTRL | Qt::Key_I)});
    rescanAction_ = addMenuAction(menu, tr("&Rescan library"), tr("Read the library folder again and update the index"),
                                  QStringLiteral("catalog-rescan-action"), {});
    menu->addSeparator();
    QAction* showIntegrations = addMenuAction(menu, tr("I&ntegrations"), tr("Show the Integrations tab: BlenderKit, Fab and your own files"),
                                              QStringLiteral("integrations-show"), {});
    connect(showIntegrations, &QAction::triggered, this, [this]() { right_->showTab(RightPanel::Tab::Integrations); });
    importBlenderKitAction_ = addMenuAction(menu, tr("Import from &BlenderKit..."),
                                            tr("Show the Integrations tab with the cursor in the BlenderKit address field"),
                                            QStringLiteral("integrations-blenderkit-action"), {});
    connect(importBlenderKitAction_, &QAction::triggered, this, &MainWindow::importFromBlenderKit);
    importFabAction_ = addMenuAction(menu, tr("Import &Fab zips..."), tr("Pick zips downloaded from Fab and check what is taken from each"),
                                     QStringLiteral("integrations-fab-action"), {});
    connect(importFabAction_, &QAction::triggered, this, [this]() {
        if (storeImports_)
            storeImports_->importFiles(QString::fromLatin1(plynth::kFabIntegrationId.data(), static_cast<qsizetype>(plynth::kFabIntegrationId.size())), false);
    });
    menu->addSeparator();
    QAction* showMaterials = addMenuAction(menu, tr("&Materials catalog"), tr("Show the Materials tab (Ctrl+Shift+M)"),
                                           QStringLiteral("materials-show"),
                                           {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M)});
    connect(showMaterials, &QAction::triggered, this, [this]() { right_->showTab(RightPanel::Tab::Materials); });
    importMaterialsAction_ = addMenuAction(menu, tr("Import &materials..."), tr("Open the screen that imports textures and .blend materials"),
                                           QStringLiteral("materials-import-action"), {});
    addSamplesAction_ = addMenuAction(menu, tr("Add the &sample materials"),
                                      tr("Import the nine sample materials that come with Plynth"),
                                      QStringLiteral("materials-samples-action"), {});
    rescanMaterialsAction_ = addMenuAction(menu, tr("Rescan m&aterials"),
                                           tr("Read the materials folder again and update the index"),
                                           QStringLiteral("materials-rescan-action"), {});
    menu->addSeparator();
    openGeneratorsAction_ = addMenuAction(menu, tr("Open the &generators folder"),
                                          tr("Open the folder of generator files in Explorer"),
                                          QStringLiteral("generators-open-folder-action"), {});
    connect(openGeneratorsAction_, &QAction::triggered, this, &MainWindow::openGeneratorsFolder);
    reloadGeneratorsAction_ = addMenuAction(menu, tr("Reload g&enerators"),
                                            tr("Read the generators folder again"),
                                            QStringLiteral("generators-reload-action"), {});
    connect(reloadGeneratorsAction_, &QAction::triggered, this, [this]() {
        host_->reloadGenerators();
        statusBar_->showMessage(tr("%1 generators in the library.").arg(host_->generators().generators.size()));
    });
    checkAddonsAction_ = addMenuAction(menu, tr("Check &add-ons"),
                                       tr("Ask Blender which add-ons are enabled, so generators that need one show as available"),
                                       QStringLiteral("generators-check-addons-action"), {});
    connect(checkAddonsAction_, &QAction::triggered, this, [this]() {
        statusBar_->showMessage(tr("Asking Blender which add-ons are enabled..."));
        host_->checkAddons();
    });
    connect(host_, &LibraryHost::addonCheckFinished, this, [this](const QString& text) { statusBar_->showMessage(text); });
    addSampleGeneratorsAction_ = addMenuAction(menu, tr("Add the sample gene&rators"),
                                               tr("Copy the sample generators that come with Plynth into the library"),
                                               QStringLiteral("generators-samples-action"), {});
    connect(addSampleGeneratorsAction_, &QAction::triggered, this,
            [this]() { statusBar_->showMessage(host_->addSampleGenerators()); });
    menu->addSeparator();
    addShapesAction_ = addMenuAction(menu, tr("Add the sample &shapes"),
                                     tr("Copy the sample opening shapes, such as the keyhole, into the library"),
                                     QStringLiteral("catalog-sample-shapes"), {});
    connect(addShapesAction_, &QAction::triggered, this,
            [this]() { statusBar_->showMessage(host_->addSampleShapes(), false); });
    QAction* rooms = addMenuAction(menu, tr("Room &types and categories..."),
                                   tr("Edit the room types and the categories of the catalog"),
                                   QStringLiteral("catalog-room-types"), {});
    connect(rooms, &QAction::triggered, this, &MainWindow::editRoomTypes);
    QAction* folder = addMenuAction(menu, tr("&Open library folder"), tr("Open the library folder in Explorer"),
                                    QStringLiteral("catalog-open-folder-action"), {});
    connect(folder, &QAction::triggered, this, &MainWindow::openLibraryFolder);
}

// Opens Library/generators in Explorer, making it first when it is missing.
void MainWindow::openGeneratorsFolder()
{
    if (!host_->hasLibrary()) {
        showError(tr("Open generators folder"), tr("No library folder is set."));
        return;
    }
    std::error_code ignored;
    std::filesystem::create_directories(host_->paths().generators, ignored);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(host_->paths().generators.wstring())));
}

// Opens the setup check over the window and keeps it until the user closes it.
void MainWindow::openSetupCheck()
{
    if (setupDialog_) {
        setupDialog_->raise();
        return;
    }
    setupDialog_ = new SetupCheckDialog(*host_, this);
    setupDialog_->setAttribute(Qt::WA_DeleteOnClose);
    setupDialog_->open();
}

// For checks only: opens the setup check.
void MainWindow::showSetupCheck(const QString& scrollToTitle)
{
    setupScrollTo_ = scrollToTitle;
    openSetupCheck();
}

// True when no setup check is open, or the one that is open has its result.
bool MainWindow::setupPictureReady()
{
    if (setupDialog_ && setupDialog_->checkFinished() && !setupScrollTo_.isEmpty()) {
        setupDialog_->scrollToItem(setupScrollTo_);
        setupScrollTo_.clear();
        return false;
    }
    return !setupDialog_ || setupDialog_->checkFinished();
}

// Opens the setup check when the quick check says it is needed.
void MainWindow::openSetupCheckIfNeeded()
{
    const plynth::SetupReport quick = plynth::quickSetupCheck(host_->settings(), host_->settingsExisted(), appGraphics());
    if (plynth::setupNeeded(quick))
        openSetupCheck();
}

// For checks only: shows the Integrations tab.
void MainWindow::showIntegrationsTab()
{
    integrationsAsked_ = true;
    right_->showTab(RightPanel::Tab::Integrations);
}

// For checks only: fills the BlenderKit address field.
void MainWindow::setBlenderKitAddress(const QString& text)
{
    showIntegrationsTab();
    integrations_->setAddress(text);
}

// For checks only: opens the Fab dialog for one model zip.
void MainWindow::showFabImport(const QString& zip)
{
    fabDialog_ = storeImports_->openFabDialog({zip}, false);
}

// For checks only: hands one chosen file to an integration as its import button would.
void MainWindow::importIntegrationFile(const QString& integrationId, const QString& file)
{
    const plynth::Integration* integration = plynth::findIntegration(integrationId.toStdString());
    if (integration == nullptr)
        return;
    const bool material = integration->assetImporters().empty();
    // A Fab zip opens the check dialog instead of a queue row, so the picture does not wait for one.
    if (!isFabIntegration(*integration) && !material)
        importAsked_ = true;
    storeImports_->importChosenFiles(integrationId, {file}, material, false);
}

// For checks only: true when the Integrations tab was not asked for, or its check has ended.
bool MainWindow::integrationsPictureReady() const
{
    return !integrationsAsked_ || integrations_->checkSettled();
}

// Shows the Integrations tab and puts the focus in the BlenderKit address field.
void MainWindow::importFromBlenderKit()
{
    right_->showTab(RightPanel::Tab::Integrations);
    integrations_->focusAddress();
}

// Picture of the window. An open setup check is drawn in the middle over a dimmed window.
QImage MainWindow::grabPicture()
{
    QImage image = grab().toImage();
    if (setupDialog_ && setupDialog_->isVisible()) {
        const Theme& t = Theme::instance();
        const QImage dialog = setupDialog_->grab().toImage();
        const QSizeF shown = QSizeF(dialog.size()) / dialog.devicePixelRatio();
        const QSizeF whole = QSizeF(image.size()) / image.devicePixelRatio();
        QPainter p(&image);
        QColor dim = t.ground();
        dim.setAlpha(t.readOnlyDim());
        p.fillRect(QRectF(QPointF(0, 0), whole), dim);
        p.drawImage(QRectF(QPointF((whole.width() - shown.width()) / 2, (whole.height() - shown.height()) / 2), shown),
                    dialog);
    }
    if (fabDialog_ && fabDialog_->isVisible()) {
        const Theme& t = Theme::instance();
        const QImage dialog = fabDialog_->grab().toImage();
        const QSizeF shown = QSizeF(dialog.size()) / dialog.devicePixelRatio();
        const QSizeF whole = QSizeF(image.size()) / image.devicePixelRatio();
        QPainter p(&image);
        QColor dim = t.ground();
        dim.setAlpha(t.readOnlyDim());
        p.fillRect(QRectF(QPointF(0, 0), whole), dim);
        const QRectF at(QPointF((whole.width() - shown.width()) / 2, (whole.height() - shown.height()) / 2), shown);
        p.drawImage(at, dialog);
        p.setPen(t.line());
        p.drawRect(at.adjusted(0, 0, -1, -1));
    }
    return image;
}

// Asks for model files and queues each one.
void MainWindow::importDialog()
{
    if (!host_->hasLibrary())
        return;
    QStringList patterns;
    for (const std::string& suffix : plynth::importableSuffixes())
        patterns << QStringLiteral("*") + fromCore(suffix);
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import asset"), lastAssetFolder_,
                                                            tr("Models (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (files.isEmpty())
        return;
    lastAssetFolder_ = QFileInfo(files.first()).absolutePath();
    importPaths(files);
}

// Queues files and folders for import and shows the Assets tab.
void MainWindow::importPaths(const QStringList& paths)
{
    if (!host_->hasLibrary())
        return;
    right_->showTab(RightPanel::Tab::Assets);
    for (const QString& path : paths)
        queue_->enqueueImport(path);
}

// For checks only: queues an import of a file or folder.
void MainWindow::importFile(const QString& path)
{
    importAsked_ = true;
    importPaths({path});
}

// For checks only: true when the progress row of the asked import is showing.
bool MainWindow::importPictureReady() const
{
    if (!importAsked_)
        return true;
    for (const QueueItem& item : queue_->items())
        if (item.state == QueueItem::State::Failed || (item.state == QueueItem::State::Running && item.percent > 0))
            return true;
    return false;
}

// Selects a new asset and offers its categories, or reports why an item failed.
void MainWindow::queueFinished(int kind, bool ok, const QString& assetId, const QString& message, bool thumbnailFailed)
{
    const bool isRefresh = kind == static_cast<int>(QueueItem::Kind::Refresh);
    host_->assetsEdited();
    if (!ok) {
        statusBar_->showMessage(isRefresh ? tr("The render failed. %1").arg(message) : tr("Import failed. %1").arg(message));
        return;
    }
    const std::optional<plynth::AssetRecord> record =
        host_->catalog() ? host_->catalog()->asset(assetId.toStdString()) : std::nullopt;
    if (isRefresh || !record)
        return;
    right_->showTab(RightPanel::Tab::Assets);
    assets_->selectAsset(assetId);
    const QString name = fromCore(record->name);
    if (record->categories.empty()) {
        statusBar_->showMessage(tr("Imported %1. Pick its categories.").arg(name), false);
        assets_->showCategories();
    } else {
        statusBar_->showMessage(thumbnailFailed ? tr("Imported %1. The thumbnail render failed.").arg(name)
                                                : tr("Imported %1.").arg(name),
                                thumbnailFailed);
    }
}

// Opens the room types and categories dialog.
void MainWindow::editRoomTypes()
{
    if (!host_->hasLibrary()) {
        showError(tr("Room types and categories"), tr("No library folder is set."));
        return;
    }
    RoomTypesDialog dialog(*host_, this);
    dialog.exec();
}

// Opens the library folder in Explorer.
void MainWindow::openLibraryFolder()
{
    if (!host_->hasLibrary()) {
        showError(tr("Open library folder"), tr("No library folder is set."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(host_->paths().root.wstring())));
}

// Keeps single letter shortcuts from firing while the user types in a text field or picks in a combo box.
bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && watched == QApplication::focusWidget()) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers plain = key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
        const bool textEntry = qobject_cast<QLineEdit*>(watched) || qobject_cast<QAbstractSpinBox*>(watched) ||
                               qobject_cast<QComboBox*>(watched) || qobject_cast<QTextEdit*>(watched) ||
                               qobject_cast<QPlainTextEdit*>(watched);
        if (textEntry && plain == Qt::NoModifier && !key->text().isEmpty()) {
            event->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// Redraws the room labels and the pickers after the room type list changed.
void MainWindow::roomTypesChanged()
{
    refreshOverlay();
    panel_->refresh();
}

// Adds one menu action with its tooltip, tour id and shortcuts.
QAction* MainWindow::addMenuAction(QMenu* menu, const QString& text, const QString& tip, const QString& tourId,
                                   const QList<QKeySequence>& keys)
{
    QAction* action = menu->addAction(text);
    action->setShortcuts(keys);
    action->setToolTip(tip);
    action->setProperty("tourId", tourId);
    return action;
}

// Adds one View menu action that calls a canvas function.
QAction* MainWindow::addViewAction(const QString& text, const QString& tip, const QString& tourId,
                                   const QList<QKeySequence>& keys, void (PlanCanvas::*slot)())
{
    QAction* action = addMenuAction(titleBar_->viewMenu(), text, tip, tourId, keys);
    connect(action, &QAction::triggered, canvas_, slot);
    viewActions_.append(action);
    return action;
}

// Enables the View actions only while an image is loaded.
void MainWindow::updateViewActions()
{
    const bool in3d = centreStack_ != nullptr && centreStack_->currentWidget() == preview_;
    for (QAction* action : viewActions_)
        action->setEnabled(canvas_->hasImage() && !in3d);
    if (fitAction_ != nullptr)
        fitAction_->setEnabled(in3d ? preview_->hasModel() : canvas_->hasImage());
}

// Shows the 3D page or the plan page in the central area and keeps the tabs and the menu in step.
void MainWindow::showCentreView(bool scene3d)
{
    centreStack_->setCurrentWidget(scene3d ? static_cast<QWidget*>(preview_) : static_cast<QWidget*>(splitter_));
    buildBar_->setView(scene3d ? BuildBar::View::Scene3d : BuildBar::View::Plan);
    view3dAction_->setChecked(scene3d);
    updateViewActions();
}

// Hands the two roof choices of the View menu to the 3D page, with the shown floor as the cut floor.
void MainWindow::applyPreviewRoofOptions()
{
    preview_->setRoofOptions(showRoofs3d(), cutAtFloor() ? focus_->floorId() : QString());
}

// For checks only: turns the roof sections on the plan on or off.
void MainWindow::setRoofSectionsShown(bool on)
{
    roofSectionsAction_->setChecked(on);
}

// For checks only: shows the floor of the roof section, turns roof sections on and selects it.
void MainWindow::selectRoof(const QString& id)
{
    const qsizetype colon = id.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0)
        return;
    showFloor(id.left(colon));
    roofSectionsAction_->setChecked(true);
    selection_->set(SelectionKind::RoofSection, id);
}

// For checks only: View > Show roofs in 3D.
void MainWindow::setShowRoofs3d(bool on)
{
    showRoofs3dAction_->setChecked(on);
}

// For checks only: View > Cut 3D at the shown floor.
void MainWindow::setCutAtFloor(bool on)
{
    cutAtFloorAction_->setChecked(on);
}

// True when the 3D view shows the roofs.
bool MainWindow::showRoofs3d() const
{
    return showRoofs3dAction_->isChecked();
}

// True when the 3D view is cut at the shown floor.
bool MainWindow::cutAtFloor() const
{
    return cutAtFloorAction_->isChecked();
}

// For checks only: shows the 3D page or the plan.
void MainWindow::showView(const QString& name)
{
    showCentreView(name.toLower() == QStringLiteral("3d"));
}

// For checks only: sets the camera angle of the 3D page: three-quarter or top.
void MainWindow::setPreviewCamera(const QString& name)
{
    preview_->setPreset(name.toLower() == QStringLiteral("top") ? ModelView::Preset::Top
                                                                : ModelView::Preset::ThreeQuarter);
}

// For checks only: true when the 3D page and the asset previews have drawn their models, or have none.
bool MainWindow::previewPictureReady() const
{
    return !builder_->running() && !exporter_->running() && !overlayRenderer_->busy() && (centreStack_->currentWidget() != preview_ || preview_->pictureReady()) &&
           assets_->pictureReady() &&
           tools_->pickerPreviewReady();
}

// Loads the building of the open project into the 3D page, or shows its empty state.
void MainWindow::refreshPreview()
{
    loadBuildReport();
    preview_->setProject(doc_.hasFolder() ? doc_.folder() : std::filesystem::path(), doc_.plan());
    applyPreviewRoofOptions();
    updateViewActions();
    canvas_->setBuilding3d(QImage(), QRectF());
    refreshBuilding3d();
}

// Sets the Undo and Redo texts and the enabled state of the actions from the document.
void MainWindow::updateActions()
{
    const std::string undoName = doc_.undoName();
    const std::string redoName = doc_.redoName();
    undoAction_->setText(undoName.empty() ? tr("&Undo") : tr("&Undo %1").arg(fromCore(undoName)));
    redoAction_->setText(redoName.empty() ? tr("&Redo") : tr("&Redo %1").arg(fromCore(redoName)));
    const bool editable = !doc_.isReadOnly();
    undoAction_->setEnabled(editable && doc_.canUndo());
    redoAction_->setEnabled(editable && doc_.canRedo());
    saveAction_->setEnabled(editable);
    saveAsAction_->setEnabled(editable);
    setImageAction_->setEnabled(editable);
}

// Shows the project name in the title bar, with a star while there are unsaved changes.
void MainWindow::updateTitle()
{
    const QString name = projectName();
    const QString shown = doc_.isModified() ? name + QStringLiteral(" *") : name;
    titleBar_->setProjectName(shown);
    setWindowTitle(QStringLiteral("Plynth - ") + shown);
}

// Reacts to a document change: image, title, menu state and the app state file.
void MainWindow::onDocumentChanged(unsigned changes)
{
    if (changes & (plynth::Change::Floors | plynth::Change::Folder)) {
        focus_->revalidate();
        refreshFloorTabs();
    }
    if (changes & (plynth::Change::FloorImage | plynth::Change::Floors | plynth::Change::Folder))
        refreshImage();
    if (changes & (plynth::Change::Points | plynth::Change::Walls | plynth::Change::Rooms | plynth::Change::Openings | plynth::Change::Items | plynth::Change::Scale |
                   plynth::Change::Stairs | plynth::Change::FloorImage | plynth::Change::Floors | plynth::Change::Folder))
        refreshOverlay();
    // Without a build report the export groups can still follow the plan, so the refresh never waits for one.
    buildUiTimer_->start();
    validateSelection();
    wallView_->refresh();
    updateWallView();
    if (changes & plynth::Change::ReadOnly)
        updateReadOnlyUi();
    // The lock and the export mode cannot wait for the timer. A change of folder refreshes further down.
    if ((changes & (plynth::Change::ReadOnly | plynth::Change::Settings)) && !(changes & plynth::Change::Folder))
        updateExportUi();
    floorTabs_->setEditable(!doc_.isReadOnly());
    updateAlignAction();
    tools_->documentChanged(changes);
    updateDeleteAction();
    tools_->refreshAvailability();
    panel_->refresh();
    if (changes & plynth::Change::Folder) {
        refreshPreview();
        loadExportReport();
        recoveryPending_ = false;
        terminal_->setWorkingFolder(doc_.hasFolder() ? QString::fromStdWString(doc_.folder().wstring())
                                                     : host_->projectsFolder());
        QTimer::singleShot(0, this, &MainWindow::syncWatcher);
    }
    if (changes & (plynth::Change::History | plynth::Change::Settings | plynth::Change::Folder)) {
        updateTitle();
        updateActions();
        syncAppState();
    }
}

// The filter text for the open image dialog.
QString MainWindow::imageFilter() const
{
    QString filter;
    for (const std::string_view ext : plynth::planImageExtensions())
        filter += QStringLiteral("*.") + QString::fromUtf8(ext.data(), static_cast<qsizetype>(ext.size())) + QLatin1Char(' ');
    return tr("Plan images (%1)").arg(filter.trimmed());
}

// Checks the file with core and decodes it with Qt.
bool MainWindow::decodeImage(const QString& path, QImage* image, QString* error)
{
    const plynth::PlanImageProbe probe = plynth::probePlanImage(toPath(path));
    if (!probe.ok()) {
        if (error != nullptr)
            *error = fromCore(probe.message);
        return false;
    }
    QImageReader::setAllocationLimit(512);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    *image = reader.read();
    if (image->isNull()) {
        if (error != nullptr)
            *error = tr("The image could not be decoded.");
        return false;
    }
    return true;
}

// Keeps a decoded image for the next refresh, so a new image is not decoded twice.
void MainWindow::cacheImage(const QString& path, const QImage& image)
{
    cachedImageKey_ = imageKey(path);
    cachedImage_ = image;
}

// Shows the image of the shown floor, or the reason it cannot be shown. Skips the work when the
// same file is already on the sheet, so a save does not refit the view.
void MainWindow::refreshImage()
{
    const plynth::Floor* shown = focus_->floor();
    if (shown == nullptr || !shown->image.has_value()) {
        shownImageKey_.clear();
        imageFailed_ = false;
        canvas_->clearImage();
        canvas_->setEmptyText(QString());
        statusBar_->clearImageInfo();
        updateViewActions();
        return;
    }

    const QString path = QFileInfo(fromCore(plynth::pathToUtf8(
        plynth::resolveImagePath(doc_.folder(), *shown->image)))).absoluteFilePath();
    const QString key = imageKey(path);
    if (canvas_->hasImage() && key == shownImageKey_)
        return;

    QImage image;
    if (key == cachedImageKey_ && !cachedImage_.isNull()) {
        image = cachedImage_;
    } else if (!decodeImage(path, &image, nullptr)) {
        shownImageKey_.clear();
        imageFailed_ = true;
        canvas_->clearImage();
        canvas_->setEmptyText(tr("The plan image %1 could not be opened.").arg(QDir::toNativeSeparators(path)));
        statusBar_->clearImageInfo();
        updateViewActions();
        return;
    }
    cachedImage_ = QImage();
    cachedImageKey_.clear();
    imageFailed_ = false;
    shownImageKey_ = key;
    canvas_->setEmptyText(QString());
    canvas_->setImage(image);
    statusBar_->setImageInfo(QFileInfo(path).fileName(), image.width(), image.height());
    updateViewActions();
}

// Redraws the marks of the shown floor on the canvas.
void MainWindow::refreshOverlay()
{
    tools_->refreshOverlay();
}

// Tells the canvas which item is selected.
void MainWindow::refreshSelection()
{
    canvas_->setSelection(selection_->kind(), selection_->id());
    updateToolActions();
    updateDeleteAction();
    updateWallView();
}

// Shows the wall view under the plan while View > Wall view is on and a wall or an opening is selected.
void MainWindow::updateWallView()
{
    const bool show = wallViewAction_ != nullptr && wallViewAction_->isChecked() && wallView_->hasTarget();
    flipWallViewAction_->setEnabled(show);
    if (wallView_->isHidden() == !show)
        return;
    wallView_->setVisible(show);
    if (show)
        QTimer::singleShot(0, this, &MainWindow::sizeWallView);
}

// Gives the wall view its first height, about two fifths of the area, once the window has a real size. After
// that the splitter keeps whatever the user drags it to.
void MainWindow::sizeWallView()
{
    if (wallViewSized_ || !isVisible() || wallView_->isHidden() || splitter_->height() <= 0)
        return;
    wallViewSized_ = true;
    const int total = splitter_->height();
    const int below = std::max(Theme::instance().wallViewMinHeight(), total * 2 / 5);
    splitter_->setSizes({total - below, below});
}

// Deletes the selected item with everything that depends on it, as one undo step.
void MainWindow::deleteSelection()
{
    if (selection_->isEmpty() || selection_->kind() == SelectionKind::RoofSection || doc_.isReadOnly())
        return;
    const std::string id = selection_->id().toStdString();
    plynth::DeleteBuild build = plynth::makeDeleteCommand(doc_.plan(), std::span<const plynth::Id>(&id, 1));
    if (!build.status.ok()) {
        statusBar_->showMessage(fromCore(build.status.message));
        return;
    }
    const plynth::Status status = doc_.execute(std::move(build.command));
    if (!status.ok())
        statusBar_->showMessage(fromCore(status.message));
}

// Enables Delete only with a selection on an editable project.
void MainWindow::updateDeleteAction()
{
    if (deleteAction_ != nullptr)
        deleteAction_->setEnabled(!selection_->isEmpty() && selection_->kind() != SelectionKind::RoofSection && !doc_.isReadOnly());
}

// Clears the selection when its item is gone, for example after undo or a reload.
void MainWindow::validateSelection()
{
    if (selection_->isEmpty())
        return;
    const plynth::Floor* floor = focus_->floor();
    const std::string id = selection_->id().toStdString();
    bool found = false;
    if (floor != nullptr) {
        switch (selection_->kind()) {
        case SelectionKind::Point:
            found = plynth::findPoint(*floor, id) != nullptr;
            break;
        case SelectionKind::Wall:
            for (const plynth::Wall& wall : floor->walls)
                found = found || wall.id == id;
            break;
        case SelectionKind::Room:
            for (const plynth::Room& room : floor->rooms)
                found = found || room.id == id;
            break;
        case SelectionKind::Opening:
            found = plynth::findOpening(*floor, id) != nullptr;
            break;
        case SelectionKind::Item:
            found = plynth::findItem(*floor, id) != nullptr;
            break;
        case SelectionKind::Stair:
            for (const plynth::Stair& stair : floor->stairs)
                found = found || stair.id == id;
            break;
        case SelectionKind::RoofSection:
            found = id.compare(0, floor->id.size() + 1, floor->id + ":") == 0;
            break;
        case SelectionKind::None:
            break;
        }
    }
    if (!found)
        selection_->clear();
}

// Checks the active tool in the Edit menu and enables the tools that can be used.
void MainWindow::updateToolActions()
{
    for (ToolId id : kStripTools) {
        QAction* action = toolActions_[static_cast<size_t>(id)];
        if (action == nullptr)
            continue;
        const bool available = tools_->isAvailable(id);
        action->setEnabled(available);
        action->setChecked(tools_->active() == id);
        QString tip = tr("%1 tool (%2)").arg(ToolStrip::toolName(id), ToolStrip::toolKey(id));
        if (!available)
            tip += QStringLiteral(". ") + tools_->unavailableReason(id);
        action->setToolTip(tip);
    }
    if (acceptDetectionAction_ != nullptr)
        acceptDetectionAction_->setEnabled(tools_->detection().canAccept() && !doc_.isReadOnly());
    const bool itemSelected = selection_ != nullptr && selection_->kind() == SelectionKind::Item && !doc_.isReadOnly();
    if (turnLeftAction_ != nullptr)
        turnLeftAction_->setEnabled(itemSelected);
    if (turnRightAction_ != nullptr)
        turnRightAction_->setEnabled(itemSelected);
    const plynth::Floor* floor = focus_->floor();
    const plynth::Item* item = itemSelected && floor ? plynth::findItem(*floor, selection_->id().toStdString()) : nullptr;
    if (mountAction_ != nullptr)
        mountAction_->setEnabled(item != nullptr && !item->wall.has_value());
    if (takeOffAction_ != nullptr)
        takeOffAction_->setEnabled(item != nullptr && item->wall.has_value());
    const bool assetItem = item != nullptr && !plynth::isGeneratorItem(*item) && host_ != nullptr && host_->hasLibrary();
    if (swapAction_ != nullptr)
        swapAction_->setEnabled(assetItem);
    if (updateSizeAction_ != nullptr)
        updateSizeAction_->setEnabled(assetItem);
}

// Shows the cursor position in metres, or nothing when it is off the plan or no scale is set.
void MainWindow::updateCursorText(const QPointF* scene)
{
    const std::optional<double> ppm = doc_.plan().scale.pixelsPerMetre;
    if (scene == nullptr || !ppm.has_value()) {
        statusBar_->setCursorText(QString());
        return;
    }
    statusBar_->setCursorText(QStringLiteral("x %1 m  y %2 m")
                                  .arg(scene->x() / *ppm, 0, 'f', 3)
                                  .arg(scene->y() / *ppm, 0, 'f', 3));
}

// For checks only: activates a tool by its name.
bool MainWindow::activateToolByName(const QString& name)
{
    const QString key = name.toLower();
    ToolId id = ToolId::Select;
    if (key == QStringLiteral("select"))
        id = ToolId::Select;
    else if (key == QStringLiteral("scale"))
        id = ToolId::Scale;
    else if (key == QStringLiteral("wall"))
        id = ToolId::Wall;
    else if (key == QStringLiteral("opening"))
        id = ToolId::Opening;
    else if (key == QStringLiteral("room"))
        id = ToolId::Room;
    else if (key == QStringLiteral("furniture"))
        id = ToolId::Furniture;
    else if (key == QStringLiteral("detect"))
        id = ToolId::Detect;
    else if (key == QStringLiteral("stair"))
        id = ToolId::Stair;
    else
        return false;
    tools_->setActive(id);
    return true;
}

// For checks only: shows the floor with this id.
bool MainWindow::showFloor(const QString& id)
{
    return focus_->setFloorId(id);
}

// For checks only: selects a point, wall, room, opening, item or stair by its id, switching to its floor first.
void MainWindow::selectById(const QString& id)
{
    selection_->clear();
    const std::string key = id.toStdString();
    for (const plynth::Floor& floor : doc_.plan().floors) {
        std::optional<SelectionKind> kind;
        if (plynth::findPoint(floor, key) != nullptr)
            kind = SelectionKind::Point;
        for (const plynth::Wall& wall : floor.walls)
            if (wall.id == key)
                kind = SelectionKind::Wall;
        for (const plynth::Room& room : floor.rooms)
            if (room.id == key)
                kind = SelectionKind::Room;
        if (plynth::findOpening(floor, key) != nullptr)
            kind = SelectionKind::Opening;
        if (plynth::findItem(floor, key) != nullptr)
            kind = SelectionKind::Item;
        for (const plynth::Stair& stair : floor.stairs)
            if (stair.id == key)
                kind = SelectionKind::Stair;
        if (!kind.has_value())
            continue;
        focus_->setFloorId(QString::fromStdString(floor.id));
        selection_->set(*kind, id);
        return;
    }
}

// Rebuilds the floor tabs from the plan.
void MainWindow::refreshFloorTabs()
{
    QList<QPair<QString, QString>> floors;
    for (const plynth::Floor& floor : doc_.plan().floors) {
        const QString id = fromCore(floor.id);
        floors.append({id, floor.name.empty() ? id : fromCore(floor.name)});
    }
    floorTabs_->setFloors(floors);
    floorTabs_->setShown(focus_->floorId());
}

// Adds an empty floor above the top one, or below the floor named, and shows it. The name is the next free
// "Floor N".
void MainWindow::addFloor(bool above, const QString& besideFloor)
{
    if (doc_.isReadOnly())
        return;
    const plynth::Plan& plan = doc_.plan();
    std::set<std::string> names;
    for (const plynth::Floor& floor : plan.floors)
        names.insert(floor.name);
    int number = static_cast<int>(plan.floors.size()) + 1;
    while (names.count(tr("Floor %1").arg(number).toStdString()) != 0)
        ++number;
    std::optional<std::size_t> position;
    if (!above) {
        for (std::size_t i = 0; i < plan.floors.size(); ++i)
            if (plan.floors[i].id == besideFloor.toStdString())
                position = i;
        if (!position.has_value())
            return;
    }
    auto command = std::make_unique<plynth::AddFloorCommand>(tr("Floor %1").arg(number).toStdString(), position);
    plynth::AddFloorCommand* raw = command.get();
    const plynth::Status status = doc_.execute(std::move(command));
    if (!status.ok()) {
        statusBar_->showMessage(fromCore(status.message));
        return;
    }
    focus_->setFloorId(fromCore(raw->newFloorId()));
    floorTabs_->setShown(focus_->floorId());
}

// Asks for a new name and renames the floor.
void MainWindow::renameFloor(const QString& floorId)
{
    const plynth::Floor* floor = nullptr;
    for (const plynth::Floor& candidate : doc_.plan().floors)
        if (candidate.id == floorId.toStdString())
            floor = &candidate;
    if (floor == nullptr || doc_.isReadOnly() || screenshotMode_)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename floor"), tr("Name of the floor"), QLineEdit::Normal,
                                               fromCore(floor->name), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name.toStdString() == floor->name)
        return;
    const plynth::Status status = doc_.execute(std::make_unique<plynth::SetFloorNameCommand>(floor->id, name.toStdString()));
    if (!status.ok())
        statusBar_->showMessage(fromCore(status.message));
}

// Asks first, then removes the floor with everything on it as one undo step.
void MainWindow::removeFloor(const QString& floorId)
{
    const plynth::Floor* floor = nullptr;
    for (const plynth::Floor& candidate : doc_.plan().floors)
        if (candidate.id == floorId.toStdString())
            floor = &candidate;
    if (floor == nullptr || doc_.isReadOnly() || doc_.plan().floors.size() < 2)
        return;
    if (!screenshotMode_) {
        const QString name = floor->name.empty() ? floorId : fromCore(floor->name);
        QMessageBox box(this);
        box.setIcon(QMessageBox::NoIcon);
        box.setTextFormat(Qt::PlainText);
        box.setWindowTitle(tr("Remove floor"));
        box.setText(tr("Remove %1 with its walls, rooms, openings, furniture and stairs? Undo brings it back.").arg(name));
        QPushButton* remove = box.addButton(tr("Remove"), QMessageBox::DestructiveRole);
        QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        remove->setToolTip(tr("Remove the floor"));
        cancel->setToolTip(tr("Keep the floor"));
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() != remove)
            return;
    }
    const plynth::Status status = doc_.execute(std::make_unique<plynth::RemoveFloorCommand>(floor->id));
    if (!status.ok())
        statusBar_->showMessage(fromCore(status.message));
}

// Shows the floor above (direction 1) or below (-1) the shown one, when there is one.
void MainWindow::stepFloor(int direction)
{
    const int next = focus_->index() + direction;
    const auto& floors = doc_.plan().floors;
    if (next >= 0 && next < static_cast<int>(floors.size()))
        focus_->setFloorId(fromCore(floors[static_cast<std::size_t>(next)].id));
}

// Keeps View > Align plan image and the floor stepping actions in step with the tools and the plan.
void MainWindow::updateAlignAction()
{
    if (alignImageAction_ == nullptr)
        return;
    const bool before = alignImageAction_->blockSignals(true);
    alignImageAction_->setChecked(tools_->alignImage());
    alignImageAction_->blockSignals(before);
    alignImageAction_->setEnabled(!tools_->alignRefusal() || tools_->alignImage());
    if (floorUpAction_ != nullptr) {
        floorUpAction_->setEnabled(focus_->index() + 1 < static_cast<int>(doc_.plan().floors.size()));
        floorDownAction_->setEnabled(focus_->index() > 0);
    }
}

// For checks only: turns image alignment on or off.
void MainWindow::setAlignImage(bool on)
{
    tools_->setAlignImage(on);
    updateAlignAction();
}

// Redraws what depends on the shown floor after another floor was chosen.
void MainWindow::floorShown()
{
    floorTabs_->setShown(focus_->floorId());
    applyPreviewRoofOptions();
    refreshBuilding3d();
    selection_->clear();
    refreshImage();
    refreshProblemMarkers();
    wallView_->refresh();
    updateWallView();
    updateDeleteAction();
    updateAlignAction();
    panel_->refresh();
}

// For checks only: copies the sample generators and shows the answer.
void MainWindow::addSampleGenerators()
{
    statusBar_->showMessage(host_->addSampleGenerators());
}

// For checks only: runs the add-on check and keeps the event loop going until it reports.
void MainWindow::checkAddonsAndWait()
{
    QEventLoop loop;
    connect(host_, &LibraryHost::addonCheckFinished, &loop, &QEventLoop::quit);
    host_->checkAddons();
    loop.exec();
}

// For checks only: runs a parameter edit through the Properties panel.
bool MainWindow::setGeneratorParam(const QString& assignment)
{
    QString message;
    const bool ok = panel_->setGeneratorParamText(assignment, &message);
    if (!ok)
        statusBar_->showMessage(message);
    return ok;
}

// For checks only: opens the furniture picker over the plan and hovers an asset like the mouse would.
bool MainWindow::showFurniturePicker(double x, double y, const QString& hoverAsset)
{
    return tools_->showFurniturePicker(x, y, hoverAsset);
}

// For checks only: shows the Assets tab.
void MainWindow::showAssetsTab()
{
    right_->showTab(RightPanel::Tab::Assets);
}

// For checks only: selects an asset in the Assets tab.
bool MainWindow::selectAsset(const QString& id)
{
    showAssetsTab();
    return assets_->selectAsset(id);
}

// For checks only: scrolls the asset details to the category list.
void MainWindow::showAssetCategories()
{
    assets_->showCategories();
}

// Shows the material import screen over the work area.
void MainWindow::showMaterialImport()
{
    if (!host_->hasLibrary())
        return;
    terminal_->close();
    pages_->setCurrentWidget(materialImport_);
    statusBar_->setHint(QString());
}

// Queues every folder of samples/materials, found by walking up from the program folder as the home screen does.
void MainWindow::addSampleMaterials()
{
    if (!host_->hasLibrary())
        return;
    QDir dir(QCoreApplication::applicationDirPath());
    QString folder;
    for (int level = 0; level < 8 && folder.isEmpty(); ++level) {
        if (QFileInfo(dir.filePath(QStringLiteral("samples/materials"))).isDir())
            folder = dir.filePath(QStringLiteral("samples/materials"));
        else if (!dir.cdUp())
            break;
    }
    if (folder.isEmpty()) {
        statusBar_->showMessage(tr("The sample materials folder was not found next to the program."));
        return;
    }
    materialImportAsked_ = true;
    right_->showTab(RightPanel::Tab::Materials);
    int count = 0;
    for (const QFileInfo& info : QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        plynth::MaterialImportRequest request;
        request.source.file = std::filesystem::path(info.absoluteFilePath().toStdWString());
        request.skipExisting = true;
        materialQueue_->enqueueImport(request);
        ++count;
    }
    statusBar_->showMessage(tr("Adding %1 sample materials.").arg(count), false);
}

// For checks only: adds a path to the import screen list.
void MainWindow::addMaterialSource(const QString& path)
{
    materialImport_->addSources({path});
}

// For checks only: true when no material import was asked for, or everything has been imported and rendered.
bool MainWindow::materialsPictureReady() const
{
    return !materialImportAsked_ || (!materialQueue_->busy() && !materialRefresher_->busy());
}

// Selects a new material and asks for its groups, or reports why an import failed.
void MainWindow::materialImportFinished(bool ok, const QString& materialId, const QString& message, bool skipped,
                                        bool thumbnailFailed)
{
    host_->materialsEdited();
    if (!ok) {
        statusBar_->showMessage(tr("Material import failed. %1").arg(message));
        return;
    }
    if (skipped || materialId.isEmpty() || host_->materialCatalog() == nullptr)
        return;
    const std::optional<plynth::MaterialRecord> record = host_->materialCatalog()->material(materialId.toStdString());
    if (!record)
        return;
    right_->showTab(RightPanel::Tab::Materials);
    materials_->selectMaterial(materialId);
    const QString name = fromCore(record->name);
    if (record->groups.empty()) {
        statusBar_->showMessage(tr("Imported %1. Pick its groups.").arg(name), false);
        materials_->showGroups();
    } else {
        statusBar_->showMessage(thumbnailFailed ? tr("Imported %1. The thumbnail render failed.").arg(name)
                                                : tr("Imported %1.").arg(name),
                                thumbnailFailed);
    }
}

// Takes one material of an asset asset.blend into the materials catalog.
void MainWindow::addMaterialFromAsset(const QString& assetId, const QString& materialName)
{
    if (!host_->hasLibrary() || host_->catalog() == nullptr)
        return;
    const std::optional<plynth::AssetRecord> record = host_->catalog()->asset(assetId.toStdString());
    if (!record)
        return;
    plynth::MaterialImportRequest request;
    request.source.file = record->folder / "asset.blend";
    request.blendMaterial = materialName.toUtf8().toStdString();
    materialImportAsked_ = true;
    materialQueue_->enqueueImport(request);
    statusBar_->showMessage(tr("Adding the material %1 to the materials catalog.").arg(materialName), false);
}

// For checks only: shows the Materials tab.
void MainWindow::showMaterialsTab()
{
    right_->showTab(RightPanel::Tab::Materials);
}

// For checks only: selects a material in the Materials tab.
bool MainWindow::selectMaterial(const QString& id)
{
    showMaterialsTab();
    return materials_->selectMaterial(id);
}

// For checks only: shows Properties and opens the material picker for a target.
bool MainWindow::showMaterialPicker(const QString& target, const QString& hoverMaterial, const QString& pickMaterial)
{
    right_->showTab(RightPanel::Tab::Properties);
    return panel_->openMaterialPicker(target, hoverMaterial, pickMaterial);
}

// For checks only: chooses the side the wall view is seen from.
void MainWindow::setWallViewSide(plynth::WallSide side)
{
    wallView_->setSide(side);
}

// Opens a project folder with core and makes it the document. A project with an active command
// line session starts read only.
bool MainWindow::openProject(const QString& folder, QString* error)
{
    const plynth::Status status = doc_.open(toPath(folder));
    if (!status.ok()) {
        if (error != nullptr)
            *error = fromCore(status.message);
        return false;
    }
    syncWatcher();
    recordRecent();
    showWork();
    return true;
}

// Points the watcher at the document's folder and checks the lock state at once.
void MainWindow::syncWatcher()
{
    watcher_->setFolder(doc_.hasFolder() ? doc_.folder() : std::filesystem::path());
    watcher_->acknowledge();
    reloadPending_ = false;
    handleCheck();
}

// Reads the project files again after an outside change. Keeps trying while the files cannot be read.
bool MainWindow::reloadFromDisk()
{
    const plynth::Status status = doc_.reload();
    reloadPending_ = !status.ok();
    if (status.ok())
        watcher_->acknowledge();
    return status.ok();
}

// Decides what to do about the lock and about outside changes to the project files.
void MainWindow::handleCheck()
{
    if (!doc_.hasFolder()) {
        watcher_->setAlwaysCheck(false);
        return;
    }
    const plynth::SessionRead session = plynth::readSession(doc_.folder(), plynth::sessionNow());
    const bool locked = session.state == plynth::SessionState::Active;
    const bool outside = watcher_->outsideChange();

    if (locked) {
        banner_->setTool(fromCore(session.lock.tool));
        if (!doc_.isReadOnly())
            doc_.setReadOnly(true, "The project is being edited from the command line by " + session.lock.tool + ".");
        if (outside || reloadPending_)
            reloadOrAsk();
        watcher_->setAlwaysCheck(true);
        return;
    }

    if (doc_.isReadOnly()) {
        if (outside || reloadPending_)
            reloadOrAsk();
        doc_.setReadOnly(false);
        watcher_->setAlwaysCheck(reloadPending_);
        if (recoveryPending_) {
            recoveryPending_ = false;
            QTimer::singleShot(0, this, &MainWindow::offerRecovery);
        }
        return;
    }

    watcher_->setAlwaysCheck(reloadPending_);
    if (!outside && !reloadPending_)
        return;
    reloadOrAsk();
}

// Reloads after an outside change. When there are unsaved changes, asks first: Load or Keep mine.
void MainWindow::reloadOrAsk()
{
    if (!doc_.isModified()) {
        reloadFromDisk();
        return;
    }
    if (screenshotMode_ || asking_)
        return;

    asking_ = true;
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setTextFormat(Qt::PlainText);
    box.setWindowTitle(tr("Project changed"));
    box.setText(tr("The plan was changed outside Plynth. Load the new version? Your unsaved changes will be lost."));
    QPushButton* load = box.addButton(tr("Load"), QMessageBox::AcceptRole);
    QPushButton* keep = box.addButton(tr("Keep mine"), QMessageBox::RejectRole);
    load->setToolTip(tr("Load the changed project and drop your unsaved changes"));
    keep->setToolTip(tr("Keep your version. The next save replaces the outside change."));
    box.setDefaultButton(keep);
    box.exec();
    asking_ = false;
    if (box.clickedButton() == load) {
        reloadFromDisk();
    } else {
        reloadPending_ = false;
        watcher_->acknowledge();
    }
}

// Shows or hides the banner and the grey sheet, and greys the editing actions.
void MainWindow::updateReadOnlyUi()
{
    const bool readOnly = doc_.isReadOnly();
    banner_->setVisible(readOnly);
    canvas_->setDimmed(readOnly);
    updateActions();
    tools_->refreshAvailability();
    panel_->refresh();
}

// Ends the command line session after a question.
void MainWindow::unlockNow()
{
    if (!doc_.hasFolder())
        return;
    if (!screenshotMode_) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::NoIcon);
        box.setTextFormat(Qt::PlainText);
        box.setWindowTitle(tr("Unlock now"));
        box.setText(tr("Unlocking ends the command line session. Its next edit will be refused until it begins a new one."));
        QPushButton* unlock = box.addButton(tr("Unlock"), QMessageBox::AcceptRole);
        QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        unlock->setToolTip(tr("End the session and make the project editable"));
        cancel->setToolTip(tr("Keep the project read only"));
        box.setDefaultButton(cancel);
        box.exec();
        if (box.clickedButton() != unlock)
            return;
    }
    const plynth::Status status = plynth::removeSession(doc_.folder());
    if (!status.ok()) {
        showError(tr("Cannot unlock"), fromCore(status.message));
        return;
    }
    handleCheck();
}

// Shows the home screen. The project that is open stays open behind it.
void MainWindow::showHome()
{
    terminal_->close();
    const bool open = doc_.hasFolder() || canvas_->hasImage() || doc_.isModified();
    home_->setOpenProject(open ? projectName() : QString());
    home_->refresh();
    pages_->setCurrentWidget(home_);
    statusBar_->setHint(QString());
}

// Shows the plan, tools and panels.
void MainWindow::showWork()
{
    pages_->setCurrentWidget(work_);
    statusBar_->setHint(tools_->hint());
}

// Asks about unsaved changes, starts an empty project and shows the home screen.
void MainWindow::closeProject()
{
    if (!confirmDiscardChanges())
        return;
    terminal_->shutdown();
    doc_.replace(plynth::makeNewProject(), {}, false);
    showHome();
}

// Opens a project from the recent list.
void MainWindow::openRecent(const QString& folder)
{
    if (!confirmDiscardChanges())
        return;
    QString error;
    if (!openProject(folder, &error))
        showError(tr("Cannot open project"), error);
}

// Puts the open project on the recent list and saves its small picture for the home screen.
void MainWindow::recordRecent()
{
    if (screenshotMode_ || !doc_.hasFolder())
        return;
    host_->recordProject(doc_.folder(), doc_.project().settings.name);
    writeRecentThumbnail(doc_.folder());
    home_->refresh();
}

// Starts an untitled project whose first floor already has the image.
bool MainWindow::newFromImage(const QString& path, QString* error)
{
    QImage image;
    if (!decodeImage(path, &image, error))
        return false;
    plynth::Project project = plynth::makeNewProject();
    project.plan.floors.front().image = plynth::pathToUtf8(std::filesystem::absolute(toPath(path)));
    cacheImage(path, image);
    doc_.replace(std::move(project), {}, false);
    showWork();
    return true;
}

// Sets the floor image with a command so it can be undone.
bool MainWindow::setPlanImage(const QString& path, QString* error)
{
    if (doc_.isReadOnly()) {
        if (error != nullptr)
            *error = tr("The project is read only while the command line edits it.");
        return false;
    }
    QImage image;
    if (!decodeImage(path, &image, error))
        return false;
    cacheImage(path, image);
    const plynth::Status status = doc_.execute(std::make_unique<plynth::SetFloorImageCommand>(
        focus_->floorId().toStdString(), plynth::pathToUtf8(std::filesystem::absolute(toPath(path)))));
    if (!status.ok()) {
        cachedImage_ = QImage();
        cachedImageKey_.clear();
        if (error != nullptr)
            *error = fromCore(status.message);
        return false;
    }
    return true;
}

// Asks before leaving a project with unsaved changes. Returns false when the user cancels.
bool MainWindow::confirmDiscardChanges()
{
    if (screenshotMode_)
        return true;
    if (terminal_->hasRunningProgram()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::NoIcon);
        box.setTextFormat(Qt::PlainText);
        box.setWindowTitle(tr("Terminal"));
        box.setText(tr("A program is still running in the terminal. Closing the project will stop it."));
        QPushButton* stop = box.addButton(tr("Stop it"), QMessageBox::AcceptRole);
        QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        stop->setToolTip(tr("Stop the program and close the project"));
        cancel->setToolTip(tr("Go back to the project"));
        box.setDefaultButton(cancel);
        box.exec();
        if (box.clickedButton() != stop)
            return false;
    }
    if (doc_.isModified()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::NoIcon);
        box.setTextFormat(Qt::PlainText);
        box.setWindowTitle(tr("Unsaved changes"));
        box.setText(tr("Save changes to %1?").arg(projectName()));
        QPushButton* save = box.addButton(tr("Save"), QMessageBox::AcceptRole);
        QPushButton* discard = box.addButton(tr("Don't save"), QMessageBox::DestructiveRole);
        QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        save->setToolTip(tr("Save the project, then continue"));
        discard->setToolTip(tr("Continue without saving. The changes are lost."));
        cancel->setToolTip(tr("Go back to the project"));
        box.setDefaultButton(save);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() == cancel)
            return false;
        if (box.clickedButton() == save && !saveDocument())
            return false;
    }
    if (doc_.hasFolder() && !recoveryPending_)
        doc_.discardAutosave();
    return true;
}

// Opens the terminal and types the text, for checks of the terminal without a person.
void MainWindow::startTerminalCheck(const QString& text)
{
    terminal_->typeWhenReady(text);
}

// Says whether a picture of the terminal would show the result of the typed text.
bool MainWindow::terminalPictureReady() const
{
    return terminal_->pictureReady();
}

// Says whether a picture would show the first detection result, or the Detect tool is not in use.
bool MainWindow::detectionPictureReady() const
{
    return !tools_->detection().active() || tools_->detection().settled();
}

// Says whether the shell could not be started.
bool MainWindow::terminalFailed() const
{
    return terminal_->startFailed();
}

// Starts an empty untitled project after the save question.
void MainWindow::newProject()
{
    if (!confirmDiscardChanges())
        return;
    terminal_->shutdown();
    doc_.replace(plynth::makeNewProject(), {}, false);
    showWork();
}

// Asks for an image and starts an untitled project from it.
void MainWindow::newProjectFromImage()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("New project from image"), lastFolder_, imageFilter());
    if (file.isEmpty())
        return;
    lastFolder_ = QFileInfo(file).absolutePath();
    if (!confirmDiscardChanges())
        return;
    QString error;
    if (!newFromImage(file, &error)) {
        showError(tr("Cannot open image"), error);
        return;
    }
    terminal_->shutdown();
}

// Asks for a project folder and opens it after the save question.
void MainWindow::openProjectDialog()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open project"), lastFolder_);
    if (folder.isEmpty())
        return;
    lastFolder_ = QFileInfo(folder).absolutePath();
    if (!confirmDiscardChanges())
        return;
    QString error;
    if (!openProject(folder, &error)) {
        showError(tr("Cannot open project"), error);
        return;
    }
    terminal_->shutdown();
    offerRecovery();
}

// Asks for an image file and sets it on the floor.
void MainWindow::setImageDialog()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Set plan image"), lastFolder_, imageFilter());
    if (file.isEmpty())
        return;
    lastFolder_ = QFileInfo(file).absolutePath();
    QString error;
    if (!setPlanImage(file, &error))
        showError(tr("Cannot open image"), error);
}

// Saves to the project folder, or asks for one when the project has none yet.
bool MainWindow::saveDocument()
{
    if (!doc_.hasFolder())
        return saveDocumentAs();
    plynth::SaveReport report;
    const plynth::Status status = doc_.save(&report);
    if (!status.ok()) {
        showError(tr("Cannot save"), fromCore(status.message));
        return false;
    }
    watcher_->acknowledge();
    reportMissingImages(report);
    recordRecent();
    return true;
}

// Asks for a folder and saves the project there.
bool MainWindow::saveDocumentAs()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Save project as"), lastFolder_);
    if (folder.isEmpty())
        return false;
    lastFolder_ = QFileInfo(folder).absolutePath();
    plynth::SaveReport report;
    const plynth::Status status = doc_.saveAs(toPath(folder), &report);
    if (!status.ok()) {
        showError(tr("Cannot save"), fromCore(status.message));
        return false;
    }
    reportMissingImages(report);
    recordRecent();
    return true;
}

// Says in the status bar when a save kept the path of a plan image that cannot be found.
void MainWindow::reportMissingImages(const plynth::SaveReport& report)
{
    if (!report.missingImages.empty())
        statusBar_->showMessage(tr("The plan image %1 is missing. Its path was kept.").arg(fromCore(report.missingImages.front())));
}

// Offers to bring back the unsaved changes that an earlier session left in the autosave.
void MainWindow::offerRecovery()
{
    if (screenshotMode_ || !doc_.hasFolder())
        return;
    if (doc_.isReadOnly()) {
        recoveryPending_ = true;
        return;
    }
    const plynth::AutosaveInfo info = plynth::findAutosave(doc_.folder());
    if (!info.exists)
        return;

    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(info.time);
    const QDateTime when = QDateTime::fromSecsSinceEpoch(
        std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count());
    QString text = tr("This project has unsaved changes from %1. Recover them?")
                       .arg(QLocale().toString(when, QLocale::ShortFormat));
    if (info.olderThanSaved)
        text += QLatin1Char(' ') + tr("The saved plan is newer.");

    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setTextFormat(Qt::PlainText);
    box.setWindowTitle(tr("Recover changes"));
    box.setText(text);
    QPushButton* recover = box.addButton(tr("Recover"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    recover->setToolTip(tr("Load the unsaved changes"));
    discard->setToolTip(tr("Delete the unsaved changes and keep the saved project"));
    box.setDefaultButton(recover);
    box.exec();

    if (box.clickedButton() == recover) {
        plynth::ProjectLoad load = plynth::loadAutosave(doc_.folder());
        if (!load.status.ok()) {
            showError(tr("Cannot recover"), fromCore(load.status.message));
            return;
        }
        const std::filesystem::path folder = doc_.folder();
        doc_.replace(std::move(load.project), folder, true);
    } else {
        doc_.discardAutosave();
    }
}

// Writes the autosave when there are changes it does not have yet.
void MainWindow::runAutosave()
{
    if (screenshotMode_)
        return;
    if (doc_.needsAutosave())
        doc_.autosave();
}

// Keeps app-state.json in the project folder in step with the open project and its unsaved state.
void MainWindow::syncAppState()
{
    if (screenshotMode_)
        return;
    const bool hasFolder = doc_.hasFolder();
    if (stateFolder_ != (hasFolder ? doc_.folder() : std::filesystem::path())) {
        removeAppState();
        if (hasFolder)
            stateFolder_ = doc_.folder();
    }
    if (!hasFolder)
        return;
    const bool modified = doc_.isModified();
    if (stateModified_.has_value() && *stateModified_ == modified)
        return;
    plynth::AppState state;
    state.process = self_;
    state.modified = modified;
    state.updated = plynth::sessionNow();
    if (plynth::writeAppState(stateFolder_, state).ok())
        stateModified_ = modified;
}

// Removes app-state.json of the folder this window last wrote it in.
void MainWindow::removeAppState()
{
    if (!stateFolder_.empty())
        plynth::removeAppState(stateFolder_, self_);
    stateFolder_.clear();
    stateModified_.reset();
}

// Asks about unsaved changes, then lets the window close.
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    terminal_->shutdown();
    removeAppState();
    event->accept();
}

// Shows a themed message dialog with a core sentence or another reason.
void MainWindow::showError(const QString& title, const QString& message)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setTextFormat(Qt::PlainText);
    box.setWindowTitle(title);
    box.setText(message);
    box.setStandardButtons(QMessageBox::Ok);
    box.button(QMessageBox::Ok)->setToolTip(tr("Close this message"));
    box.exec();
}

// Accepts a drag only when it carries a local file with a plan image extension.
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() && !doc_.isReadOnly()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile() && plynth::hasPlanImageExtension(toPath(url.toLocalFile()))) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

// Sets the first dropped file that has a plan image extension as the plan image.
void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString file = url.toLocalFile();
        if (url.isLocalFile() && plynth::hasPlanImageExtension(toPath(file))) {
            event->acceptProposedAction();
            QString error;
            if (!setPlanImage(file, &error))
                showError(tr("Cannot open image"), error);
            return;
        }
    }
    event->ignore();
}

// Maximizes the window, or restores it when it is already maximized.
void MainWindow::toggleMaximized()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

// Keeps the maximize button icon in step with the window state.
void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && titleBar_ != nullptr)
        titleBar_->setMaximized(isMaximized());
}

// Gives the frameless window its Windows snap, shadow and resize behavior once it has a handle.
void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, &MainWindow::sizeWallView);
#ifdef Q_OS_WIN
    if (!nativeFrameReady_) {
        nativeFrameReady_ = true;
        win::enableNativeFrame(*this);
    }
#endif
}

// Passes window messages to the Windows code for hit testing and frame handling.
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (nativeFrameReady_ && titleBar_ != nullptr && win::handleNativeEvent(*this, *titleBar_, message, result))
        return true;
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// Shows a small dialog with the app name and version from core.
void MainWindow::showAbout()
{
    const Theme& t = Theme::instance();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About Plynth"));
    dialog.setWindowIcon(windowIcon());

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(t.unit() * 6, t.unit() * 6, t.unit() * 6, t.unit() * 6);
    layout->setSpacing(t.unit() * 2);

    auto* logo = new QLabel(&dialog);
    logo->setPixmap(QPixmap(QStringLiteral(":/logo.png"))
                        .scaled(QSize(t.logoSize(), t.logoSize()) * 3, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto* name = new QLabel(tr("Plynth"), &dialog);
    name->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    name->setAlignment(Qt::AlignCenter);
    layout->addWidget(name);

    const QString version = QString::fromUtf8(plynth::version().data(),
                                              static_cast<qsizetype>(plynth::version().size()));
    auto* versionLabel = new QLabel(tr("Version %1").arg(version), &dialog);
    versionLabel->setFont(t.monoFont(t.sizeNormal()));
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setToolTip(tr("The Plynth version"));
    layout->addWidget(versionLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    buttons->button(QDialogButtonBox::Ok)->setToolTip(tr("Close this dialog"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

}
