#include "MainWindow.h"

#include "TitleBar.h"
#include "canvas/PlanCanvas.h"
#include "theme/Theme.h"

#include <plynth/core/Version.h>

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QMenu>
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

    canvas_ = new PlanCanvas(this);
    setCentralWidget(canvas_);

    auto* exitAction =titleBar_->fileMenu()->addAction(tr("E&xit"));
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
