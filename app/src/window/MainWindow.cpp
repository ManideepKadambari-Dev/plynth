#include "MainWindow.h"

#include "TitleBar.h"
#include "StatusBar.h"
#include "canvas/OverlayBuilder.h"
#include "canvas/PlanCanvas.h"
#include "ReadOnlyBanner.h"
#include "document/DocumentBridge.h"
#include "detection/DetectionSession.h"
#include "document/Selection.h"
#include "panels/PropertiesPanel.h"
#include "panels/ToolStrip.h"
#include "tools/ToolController.h"
#include "document/ProjectWatcher.h"
#include "terminal/TerminalDock.h"
#include "wallview/WallView.h"
#include "theme/Theme.h"

#include <plynth/core/PlanImage.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Version.h>
#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/FloorCommands.h>

#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QPixmap>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QSplitter>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>

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
    canvas_ = new PlanCanvas(central);
    canvas_->setAcceptDrops(false);
    canvas_->viewport()->setAcceptDrops(false);
    statusBar_ = new StatusBar(central);
    banner_ = new ReadOnlyBanner(central);
    banner_->hide();
    selection_ = new Selection(this);
    tools_ = new ToolController(doc_, canvas_, selection_, this, this);
    toolStrip_ = new ToolStrip(tools_, central);
    panel_ = new PropertiesPanel(doc_, *selection_, *tools_, central);
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
    row->addWidget(splitter_, 1);
    panel_->setParent(work);
    row->addWidget(panel_);
    column->addWidget(banner_);
    column->addWidget(work, 1);
    column->addWidget(statusBar_);
    setCentralWidget(central);
    setAcceptDrops(true);
    terminal_ = new TerminalDock(work, canvas_, this);

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
    openAction_ = addMenuAction(file, tr("&Open project..."), tr("Open a project folder (Ctrl+O)"),
                                QStringLiteral("file-open-project"), {QKeySequence(Qt::CTRL | Qt::Key_O)});
    connect(openAction_, &QAction::triggered, this, &MainWindow::openProjectDialog);
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
    const std::array<ToolId, kAllTools.size()>& toolIds = kAllTools;
    const std::array<Qt::Key, kAllTools.size()> toolKeys{Qt::Key_V, Qt::Key_S, Qt::Key_W, Qt::Key_O, Qt::Key_R, Qt::Key_D};
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
    addViewAction(tr("&Fit to window"), tr("Fit the whole plan in the window (Ctrl+0)"),
                  QStringLiteral("view-fit"), {QKeySequence(Qt::CTRL | Qt::Key_0)},
                  &PlanCanvas::fitToWindow);
    addViewAction(tr("&Actual size (100%)"), tr("Show the plan at 100 percent (Ctrl+1)"),
                  QStringLiteral("view-actual-size"), {QKeySequence(Qt::CTRL | Qt::Key_1)},
                  &PlanCanvas::actualSize);
    updateViewActions();
    titleBar_->viewMenu()->addSeparator();
    snapGridAction_ = addMenuAction(titleBar_->viewMenu(), tr("Snap to &grid"),
                                    tr("Snap new and moved points to the grid (G)"), QStringLiteral("view-snap-grid"),
                                    {QKeySequence(Qt::Key_G)});
    snapGridAction_->setCheckable(true);
    snapGridAction_->setChecked(tools_->snapToGrid());
    connect(snapGridAction_, &QAction::toggled, this, [this](bool on) { tools_->setSnapToGrid(on); });
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

    auto* aboutAction = titleBar_->helpMenu()->addAction(tr("&About Plynth"));
    aboutAction->setToolTip(tr("Show the Plynth version"));
    aboutAction->setProperty("tourId", QStringLiteral("help-about"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
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
    for (QAction* action : viewActions_)
        action->setEnabled(canvas_->hasImage());
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
    if (changes & (plynth::Change::FloorImage | plynth::Change::Floors | plynth::Change::Folder))
        refreshImage();
    if (changes & (plynth::Change::Points | plynth::Change::Walls | plynth::Change::Rooms | plynth::Change::Openings | plynth::Change::Scale |
                   plynth::Change::Floors | plynth::Change::Folder))
        refreshOverlay();
    validateSelection();
    wallView_->refresh();
    updateWallView();
    if (changes & plynth::Change::ReadOnly)
        updateReadOnlyUi();
    tools_->documentChanged(changes);
    updateDeleteAction();
    tools_->refreshAvailability();
    panel_->refresh();
    if (changes & plynth::Change::Folder) {
        recoveryPending_ = false;
        terminal_->setWorkingFolder(doc_.hasFolder() ? QString::fromStdWString(doc_.folder().wstring()) : QString());
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

// Shows the image of the first floor, or the reason it cannot be shown. Skips the work when the
// same file is already on the sheet, so a save does not refit the view.
void MainWindow::refreshImage()
{
    const plynth::Plan& plan = doc_.plan();
    if (plan.floors.empty() || !plan.floors.front().image.has_value()) {
        shownImageKey_.clear();
        imageFailed_ = false;
        canvas_->clearImage();
        canvas_->setEmptyText(QString());
        statusBar_->clearImageInfo();
        updateViewActions();
        return;
    }

    const QString path = QFileInfo(fromCore(plynth::pathToUtf8(
        plynth::resolveImagePath(doc_.folder(), *plan.floors.front().image)))).absoluteFilePath();
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

// Gives the canvas the points, walls and rooms of the first floor, in image pixels. Nothing without a scale.
void MainWindow::refreshOverlay()
{
    canvas_->setOverlay(buildOverlay(doc_.plan()));
    canvas_->setGridStep(tools_->gridStepPixels());
}

// Tells the canvas which item is selected.
void MainWindow::refreshSelection()
{
    canvas_->setSelection(selection_->kind(), selection_->id());
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
    if (selection_->isEmpty() || doc_.isReadOnly())
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
        deleteAction_->setEnabled(!selection_->isEmpty() && !doc_.isReadOnly());
}

// Clears the selection when its item is gone, for example after undo or a reload.
void MainWindow::validateSelection()
{
    if (selection_->isEmpty())
        return;
    const plynth::Floor* floor = doc_.plan().floors.empty() ? nullptr : &doc_.plan().floors.front();
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
    for (ToolId id : kAllTools) {
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
    else if (key == QStringLiteral("detect"))
        id = ToolId::Detect;
    else
        return false;
    tools_->setActive(id);
    return true;
}

// For checks only: selects a point, wall or room of the first floor by its id.
void MainWindow::selectById(const QString& id)
{
    selection_->clear();
    if (doc_.plan().floors.empty())
        return;
    const plynth::Floor& floor = doc_.plan().floors.front();
    const std::string key = id.toStdString();
    if (plynth::findPoint(floor, key) != nullptr) {
        selection_->set(SelectionKind::Point, id);
        return;
    }
    for (const plynth::Wall& wall : floor.walls)
        if (wall.id == key) {
            selection_->set(SelectionKind::Wall, id);
            return;
        }
    for (const plynth::Room& room : floor.rooms)
        if (room.id == key) {
            selection_->set(SelectionKind::Room, id);
            return;
        }
    if (plynth::findOpening(floor, key) != nullptr)
        selection_->set(SelectionKind::Opening, id);
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
        doc_.plan().floors.front().id, plynth::pathToUtf8(std::filesystem::absolute(toPath(path)))));
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
    const plynth::Status status = doc_.save();
    if (!status.ok()) {
        showError(tr("Cannot save"), fromCore(status.message));
        return false;
    }
    watcher_->acknowledge();
    return true;
}

// Asks for a folder and saves the project there.
bool MainWindow::saveDocumentAs()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Save project as"), lastFolder_);
    if (folder.isEmpty())
        return false;
    lastFolder_ = QFileInfo(folder).absolutePath();
    const plynth::Status status = doc_.saveAs(toPath(folder));
    if (!status.ok()) {
        showError(tr("Cannot save"), fromCore(status.message));
        return false;
    }
    return true;
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
