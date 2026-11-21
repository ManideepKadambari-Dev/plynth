#include "MainWindow.h"

#include "TitleBar.h"
#include "StatusBar.h"
#include "canvas/PlanCanvas.h"
#include "theme/Theme.h"

#include <plynth/core/PlanImage.h>
#include <plynth/core/Version.h>

#include <QAction>
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
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include "FramelessWindow_win.h"
#endif

namespace plynth::app {

// Builds the window, title bar and the first menu actions.
MainWindow::MainWindow(const QString& projectFolder, QWidget* parent)
    : QMainWindow(parent, Qt::Window | Qt::FramelessWindowHint)
{
    setWindowIcon(QIcon(QStringLiteral(":/logo.png")));
    if (projectFolder.isEmpty())
        projectName_ = tr("No project");
    else {
        const QString clean = QDir::cleanPath(projectFolder);
        projectName_ = QFileInfo(clean).fileName();
        if (projectName_.isEmpty())
            projectName_ = clean;
    }
    setWindowTitle(QStringLiteral("Plynth - ") + projectName_);

    titleBar_ = new TitleBar(this);
    titleBar_->setProjectName(projectName_);
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

    connect(canvas_, &PlanCanvas::zoomChanged, statusBar_, &StatusBar::setZoom);

    auto* openAction = titleBar_->fileMenu()->addAction(tr("&Open plan image..."));
    openAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    openAction->setToolTip(tr("Open a plan image (Ctrl+O)"));
    openAction->setProperty("tourId", QStringLiteral("file-open-image"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openImageDialog);
    titleBar_->fileMenu()->addSeparator();

    addViewAction(tr("Zoom &in"), tr("Zoom in (Ctrl+=)"), QStringLiteral("view-zoom-in"),
                  {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)},
                  &PlanCanvas::zoomIn);
    addViewAction(tr("Zoom &out"), tr("Zoom out (Ctrl+-)"), QStringLiteral("view-zoom-out"),
                  {QKeySequence(Qt::CTRL | Qt::Key_Minus)}, &PlanCanvas::zoomOut);
    addViewAction(tr("&Fit to window"), tr("Fit the whole plan in the window (Ctrl+0)"),
                  QStringLiteral("view-fit"), {QKeySequence(Qt::CTRL | Qt::Key_0)},
                  &PlanCanvas::fitToWindow);
    addViewAction(tr("&Actual size (100%)"), tr("Show the plan at 100 percent (Ctrl+1)"),
                  QStringLiteral("view-actual-size"), {QKeySequence(Qt::CTRL | Qt::Key_1)},
                  &PlanCanvas::actualSize);
    updateViewActions();

    auto* exitAction = titleBar_->fileMenu()->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
    exitAction->setToolTip(tr("Exit Plynth (Alt+F4)"));
    exitAction->setProperty("tourId", QStringLiteral("file-exit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* aboutAction = titleBar_->helpMenu()->addAction(tr("&About Plynth"));
    aboutAction->setToolTip(tr("Show the Plynth version"));
    aboutAction->setProperty("tourId", QStringLiteral("help-about"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    connect(titleBar_, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &TitleBar::maximizeToggleRequested, this, &MainWindow::toggleMaximized);
    connect(titleBar_, &TitleBar::closeRequested, this, &QWidget::close);
}

// Adds one View menu action that calls a canvas function.
QAction* MainWindow::addViewAction(const QString& text, const QString& tip, const QString& tourId,
                                   const QList<QKeySequence>& keys, void (PlanCanvas::*slot)())
{
    QAction* action = titleBar_->viewMenu()->addAction(text);
    action->setShortcuts(keys);
    action->setToolTip(tip);
    action->setProperty("tourId", tourId);
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

// Asks for an image file and loads it, showing a message if it fails.
void MainWindow::openImageDialog()
{
    QString filter;
    for (const std::string_view ext : plynth::planImageExtensions())
        filter += QStringLiteral("*.") + QString::fromUtf8(ext.data(), static_cast<qsizetype>(ext.size())) + QLatin1Char(' ');
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Open plan image"), lastFolder_,
        tr("Plan images (%1)").arg(filter.trimmed()));
    if (file.isEmpty())
        return;
    lastFolder_ = QFileInfo(file).absolutePath();
    QString error;
    if (!loadImage(file, &error))
        showImageError(error);
}

// Checks the file with core, decodes it with Qt and shows it. Keeps the old image on failure.
bool MainWindow::loadImage(const QString& path, QString* error)
{
    const plynth::PlanImageProbe probe = plynth::probePlanImage(std::filesystem::path(path.toStdWString()));
    if (!probe.ok()) {
        if (error != nullptr)
            *error = QString::fromUtf8(probe.message.data(), static_cast<qsizetype>(probe.message.size()));
        return false;
    }

    QImageReader::setAllocationLimit(512);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        if (error != nullptr)
            *error = tr("The image could not be decoded.");
        return false;
    }

    canvas_->setImage(image);
    statusBar_->setImageInfo(QFileInfo(path).fileName(), image.width(), image.height());
    updateViewActions();
    return true;
}

// Shows a small message dialog with the reason an image was refused.
void MainWindow::showImageError(const QString& message)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(tr("Cannot open image"));
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
            if (url.isLocalFile() &&
                plynth::hasPlanImageExtension(std::filesystem::path(url.toLocalFile().toStdWString()))) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

// Loads the first dropped file that has a plan image extension.
void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString file = url.toLocalFile();
        if (url.isLocalFile() && plynth::hasPlanImageExtension(std::filesystem::path(file.toStdWString()))) {
            event->acceptProposedAction();
            QString error;
            if (!loadImage(file, &error))
                showImageError(error);
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
