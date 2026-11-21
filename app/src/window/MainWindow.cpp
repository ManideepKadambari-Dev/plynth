#include "MainWindow.h"

#include "TitleBar.h"
#include "StatusBar.h"
#include "canvas/PlanCanvas.h"
#include "document/DocumentBridge.h"
#include "theme/Theme.h"

#include <plynth/core/PlanImage.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Version.h>
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
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

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
    column->addWidget(canvas_, 1);
    column->addWidget(statusBar_);
    setCentralWidget(central);
    setAcceptDrops(true);

    connect(canvas_, &PlanCanvas::zoomChanged, this, [this](double zoom) {
        if (canvas_->hasImage())
            statusBar_->setZoom(zoom);
        else
            statusBar_->clearZoom();
    });

    buildMenus();

    connect(titleBar_, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &TitleBar::maximizeToggleRequested, this, &MainWindow::toggleMaximized);
    connect(titleBar_, &TitleBar::closeRequested, this, &QWidget::close);

    bridge_ = std::make_unique<DocumentBridge>(doc_);
    connect(bridge_.get(), &DocumentBridge::changed, this,
            [this](unsigned changes, const QString&) { onDocumentChanged(changes); });

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(plynth::kAutosaveIntervalSeconds * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::runAutosave);
    autosaveTimer_->start();

    refreshImage();
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
    if (on)
        autosaveTimer_->stop();
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
    undoAction_->setEnabled(doc_.canUndo());
    redoAction_->setEnabled(doc_.canRedo());
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

// Opens a project folder with core and makes it the document.
bool MainWindow::openProject(const QString& folder, QString* error)
{
    const plynth::Status status = doc_.open(toPath(folder));
    if (!status.ok()) {
        if (error != nullptr)
            *error = fromCore(status.message);
        return false;
    }
    return true;
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
    if (doc_.hasFolder())
        doc_.discardAutosave();
    return true;
}

// Starts an empty untitled project after the save question.
void MainWindow::newProject()
{
    if (!confirmDiscardChanges())
        return;
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
    if (!newFromImage(file, &error))
        showError(tr("Cannot open image"), error);
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
    if (event->mimeData()->hasUrls()) {
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
