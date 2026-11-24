#include "ToolWindow.h"

#include "TitleBar.h"
#include "theme/Theme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#ifdef Q_OS_WIN
#include "FramelessWindow_win.h"
#endif

namespace plynth::app {

namespace {

constexpr int kPlaceDelayMs = 400;

// Says whether enough of a frame lies on a screen to grab its title bar and move it.
bool reachable(const QRect& frame, const QScreen* screen)
{
    const Theme& t = Theme::instance();
    const QRect shown = screen->availableGeometry().intersected(frame);
    return shown.width() >= t.unit() * 16 && shown.height() >= t.titleBarHeight();
}

}

// Puts the title bar over the content and takes the content in.
ToolWindow::ToolWindow(const QString& name, const QString& title, QWidget* content, const QSize& defaultSize,
                       int columnWidth)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), name_(name), content_(content), defaultSize_(defaultSize)
{
    setWindowIcon(QIcon(QStringLiteral(":/logo.png")));
    setWindowTitle(tr("%1 - Plynth").arg(title));
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    titleBar_ = new TitleBar(this, false);
    titleBar_->setProjectName(title);
    column->addWidget(titleBar_);
    content_->setParent(this);
    if (columnWidth > 0) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        content_->setMaximumWidth(columnWidth);
        row->addStretch(1);
        row->addWidget(content_, 100);
        row->addStretch(1);
        column->addLayout(row, 1);
    } else {
        column->addWidget(content_, 1);
    }
    content_->show();
    resize(defaultSize_);

    placeTimer_ = new QTimer(this);
    placeTimer_->setSingleShot(true);
    placeTimer_->setInterval(kPlaceDelayMs);
    connect(placeTimer_, &QTimer::timeout, this, &ToolWindow::placeChanged);
    connect(titleBar_, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &TitleBar::maximizeToggleRequested, this, &ToolWindow::toggleMaximized);
    connect(titleBar_, &TitleBar::closeRequested, this, &QWidget::close);
}

// Keeps the place to open at until the window is first shown.
void ToolWindow::setStartPlace(const std::optional<plynth::WindowPlace>& place, QWidget* anchor, int step)
{
    startPlace_ = place;
    anchor_ = anchor;
    step_ = step;
}

// Shows the window, placing it the first time, and brings it to the front.
void ToolWindow::open()
{
    if (!placed_) {
        applyStartPlace();
        placed_ = true;
    }
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();
}

// Uses the remembered place when it is still on a screen, else centres the window over the anchor window on
// the anchor's screen, never larger than that screen.
void ToolWindow::applyStartPlace()
{
    const Theme& t = Theme::instance();
    if (startPlace_) {
        const QRect frame(startPlace_->x, startPlace_->y, startPlace_->width, startPlace_->height);
        bool onScreen = false;
        for (const QScreen* screen : QGuiApplication::screens())
            onScreen = onScreen || reachable(frame, screen);
        if (onScreen) {
            setGeometry(frame);
            if (startPlace_->maximized)
                setWindowState(windowState() | Qt::WindowMaximized);
            return;
        }
    }
    const QScreen* screen = anchor_ && anchor_->screen() ? anchor_->screen() : QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(QPoint(0, 0), defaultSize_);
    QSize size = startPlace_ ? QSize(startPlace_->width, startPlace_->height) : defaultSize_;
    size = size.boundedTo(area.size() * 9 / 10);
    const QPoint centre = anchor_ && anchor_->isVisible() ? anchor_->frameGeometry().center() : area.center();
    QRect frame(QPoint(0, 0), size);
    frame.moveCenter(centre + QPoint(step_, step_) * t.unit() * 6);
    frame.moveLeft(qBound(area.left(), frame.left(), std::max(area.left(), area.right() - frame.width() + 1)));
    frame.moveTop(qBound(area.top(), frame.top(), std::max(area.top(), area.bottom() - frame.height() + 1)));
    setGeometry(frame);
}

// The frame before any maximize, the screen it is on and whether it is maximized.
plynth::WindowPlace ToolWindow::place() const
{
    const QRect frame = isMaximized() ? normalGeometry() : geometry();
    plynth::WindowPlace place;
    place.x = frame.x();
    place.y = frame.y();
    place.width = std::max(1, frame.width());
    place.height = std::max(1, frame.height());
    if (const QScreen* on = screen())
        place.screen = on->name().toStdString();
    place.maximized = isMaximized();
    return place;
}

// Maximizes the window, or restores it when it is already maximized.
void ToolWindow::toggleMaximized()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

// Reports the new place a moment after the last move or resize, so a drag is saved once.
void ToolWindow::placeMoved()
{
    if (reportsPlace_ && placed_ && isVisible() && !isMinimized())
        placeTimer_->start();
}

// Paints the Panel background behind the title bar and the content.
void ToolWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::instance().panel());
}

// Gives the frameless window its Windows snap, shadow and resize behavior once it has a handle.
void ToolWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_WIN
    if (!nativeFrameReady_) {
        nativeFrameReady_ = true;
        win::enableNativeFrame(*this);
    }
#endif
}

// Notes a move for the settings.
void ToolWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    placeMoved();
}

// Notes a resize for the settings.
void ToolWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeMoved();
}

// Keeps the maximize button in step with the window state and notes the change for the settings.
void ToolWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        titleBar_->setMaximized(isMaximized());
        placeMoved();
    }
}

// Passes window messages to the Windows code for hit testing and frame handling.
bool ToolWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (nativeFrameReady_ && win::handleNativeEvent(*this, *titleBar_, message, result))
        return true;
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

}
