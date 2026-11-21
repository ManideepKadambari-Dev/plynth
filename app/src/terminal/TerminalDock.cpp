#include "TerminalDock.h"

#include "TerminalWidgets.h"
#include "theme/Theme.h"

#include <TerminalSession.h>
#include <TerminalView.h>

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kMinPanelWidth = 320;
constexpr int kMinPanelHeight = 160;
constexpr int kQuietBeforeTypingMs = 600;
constexpr qint64 kQuietMs = 1500;
constexpr qint64 kPictureLimitMs = 15000;

// Builds the terminal colors, font and spacing from the theme.
plynth::terminal::TerminalPalette makePalette()
{
    const Theme& t = Theme::instance();
    plynth::terminal::TerminalPalette palette;
    palette.background = t.ground();
    palette.foreground = t.text();
    palette.cursor = t.signalAmber();
    palette.selection = t.signalAmber();
    palette.selection.setAlpha(t.terminalSelectionAlpha());
    palette.scrollIndicator = t.muted();
    for (int i = 0; i < 16; ++i)
        palette.ansi[static_cast<size_t>(i)] = t.terminalAnsi(i);
    palette.fontFamily = t.monoFamily();
    palette.pixelSize = t.sizeNormal();
    palette.padding = t.unit() * 2;
    return palette;
}

}

// Creates the button over the area. The panel and the shell are made on first use.
TerminalDock::TerminalDock(QWidget* host, QWidget* area, QObject* parent)
    : QObject(parent), host_(host), area_(area)
{
    const Theme& t = Theme::instance();
    panelSize_ = QSize(t.terminalPanelWidth(), t.terminalPanelHeight());

    button_ = new TerminalButton(host_);
    button_->setFixedSize(button_->sizeHint());
    button_->setToolTip(tr("Show or hide the terminal (Ctrl+`)"));
    button_->setProperty("tourId", QStringLiteral("terminal-button"));
    connect(button_, &QAbstractButton::clicked, this, &TerminalDock::toggle);
    area_->installEventFilter(this);
    layoutOverlay();
}

// The session closes with its owner, which ends the shell and everything it started.
TerminalDock::~TerminalDock() = default;

// Says whether the panel is showing.
bool TerminalDock::isOpen() const
{
    return panel_ != nullptr && panel_->isVisible();
}

// Shows the panel when it is hidden and hides it when it is showing.
void TerminalDock::toggle()
{
    if (isOpen())
        close();
    else
        open();
}

// Shows the panel and gives it the keyboard. Starts the shell the first time.
void TerminalDock::open()
{
    ensureCreated();
    panel_->show();
    layoutOverlay();
    panel_->layout()->activate();
    if (!started_)
        startShell();
    button_->setChecked(true);
    view_->setFocus();
    emit openChanged(true);
}

// Hides the panel. The shell keeps running.
void TerminalDock::close()
{
    if (panel_ == nullptr)
        return;
    panel_->hide();
    button_->setChecked(false);
    area_->setFocus();
    emit openChanged(false);
}

// Remembers where the next shell should start.
void TerminalDock::setWorkingFolder(const QString& folder)
{
    folder_ = folder;
}

// Something besides the shell itself is running when the job holds more than one process.
bool TerminalDock::hasRunningProgram() const
{
    return session_ != nullptr && started_ && session_->isRunning() && session_->processCount() > 1;
}

// Ends the shell and hides the panel, so the next opening starts a new one in the right folder.
void TerminalDock::shutdown()
{
    if (session_ != nullptr)
        session_->close();
    started_ = false;
    pendingText_.clear();
    if (panel_ != nullptr && panel_->isVisible())
        close();
}

// Remembers text to type once the shell has printed something.
void TerminalDock::typeWhenReady(const QString& text)
{
    pendingText_ = text;
    typed_ = false;
    outputSinceTyped_ = false;
    waitingFirstOutput_ = true;
    sinceStart_.start();
    sinceOutput_.start();
    open();
}

// Says whether a picture of the terminal would show the result.
bool TerminalDock::pictureReady() const
{
    if (startFailed_)
        return true;
    if (sinceStart_.isValid() && sinceStart_.elapsed() >= kPictureLimitMs)
        return true;
    return typed_ && outputSinceTyped_ && sinceOutput_.isValid() && sinceOutput_.elapsed() >= kQuietMs;
}

// Builds the panel, the view and the session the first time the terminal is needed.
void TerminalDock::ensureCreated()
{
    if (panel_ != nullptr)
        return;
    const Theme& t = Theme::instance();
    panel_ = new TerminalPanel(host_);
    panel_->setToolTip(tr("Terminal"));
    panel_->setProperty("tourId", QStringLiteral("terminal-panel"));
    panel_->hide();

    session_ = new plynth::terminal::TerminalSession(24, 80, this);
    view_ = new plynth::terminal::TerminalView(session_, makePalette(), panel_);
    view_->setToolTip(tr("Terminal: your shell, started in the project folder"));
    view_->setProperty("tourId", QStringLiteral("terminal-view"));
    view_->installEventFilter(this);

    endedLabel_ = new QLabel(panel_);
    endedLabel_->setFont(t.uiFont(t.sizeSmall()));
    endedLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    endedLabel_->hide();

    auto* layout = new QVBoxLayout(panel_);
    layout->setContentsMargins(t.unit() * 2, t.unit() * 2, t.unit() * 2, t.unit() * 2);
    layout->setSpacing(t.unit());
    layout->addWidget(view_, 1);
    layout->addWidget(endedLabel_);

    connect(panel_->grip(), &TerminalGrip::sizeRequested, this, &TerminalDock::setPanelSize);
    connect(session_, &plynth::terminal::TerminalSession::finished, this, [this](int) { onFinished(); });
    connect(session_, &plynth::terminal::TerminalSession::outputReceived, this, [this]() {
        sinceOutput_.restart();
        if (typed_)
            outputSinceTyped_ = true;
        if (waitingFirstOutput_)
            typeTimer_.start();
    });
    typeTimer_.setSingleShot(true);
    typeTimer_.setInterval(kQuietBeforeTypingMs);
    connect(&typeTimer_, &QTimer::timeout, this, [this]() {
        waitingFirstOutput_ = false;
        if (!pendingText_.isNull() && session_->isRunning()) {
            session_->write((pendingText_ + QLatin1Char('\r')).toUtf8());
            sinceOutput_.restart();
            outputSinceTyped_ = false;
        }
        typed_ = true;
    });
}

// Starts the shell in the project folder, or in Documents for a project that was never saved.
bool TerminalDock::startShell()
{
    plynth::terminal::SessionOptions options;
    options.workingDir = folder_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                           : folder_;
    options.pathPrefix = QCoreApplication::applicationDirPath();
    const QSize cell = view_->cellSize();
    const int pad = Theme::instance().unit() * 2;
    options.columns = std::max(2, (view_->width() - 2 * pad) / cell.width());
    options.rows = std::max(1, (view_->height() - 2 * pad) / cell.height());

    QString error;
    endedLabel_->hide();
    startFailed_ = !session_->start(options, &error);
    started_ = !startFailed_;
    if (startFailed_) {
        endedLabel_->setText(tr("The shell could not be started. %1").arg(error));
        endedLabel_->show();
    }
    return !startFailed_;
}

// Tells the user the shell ended and how to start another.
void TerminalDock::onFinished()
{
    endedLabel_->setText(tr("The shell ended. Press Enter to start a new one."));
    endedLabel_->show();
}

// Sets the size of the panel from a drag of the grip, within the limits of the plan area.
void TerminalDock::setPanelSize(const QSize& size)
{
    panelSize_ = size;
    layoutOverlay();
}

// Puts the button and the panel in the bottom right corner of the plan area.
void TerminalDock::layoutOverlay()
{
    const Theme& t = Theme::instance();
    const QRect area = area_->geometry();
    const int gap = t.unit() * 4;
    const int d = button_->width();
    const int right = area.right() + 1 - gap;
    const int bottom = area.bottom() + 1 - gap;
    button_->move(right - d, bottom - d);
    button_->raise();
    if (panel_ == nullptr)
        return;

    panelSize_.setWidth(std::clamp(panelSize_.width(), kMinPanelWidth,
                                   std::max(kMinPanelWidth, area.width() * 9 / 10)));
    panelSize_.setHeight(std::clamp(panelSize_.height(), kMinPanelHeight,
                                    std::max(kMinPanelHeight, area.height() * 9 / 10)));
    const int w = std::min(panelSize_.width(), std::max(1, area.width() * 9 / 10));
    const int panelBottom = bottom - d - t.unit() * 2;
    const int h = std::min(panelSize_.height(), std::max(1, panelBottom - area.top() - gap));
    panel_->setGeometry(right - w, panelBottom - h, w, h);
    panel_->raise();
    button_->raise();
}

// Keeps the overlay in the corner when the area moves, and restarts a finished shell on Enter.
bool TerminalDock::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == area_) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show)
            layoutOverlay();
    } else if (watched == view_ && event->type() == QEvent::KeyPress && session_ != nullptr &&
               session_->hasExited()) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            session_->feedOutput(QByteArray("\x1b[2J\x1b[3J\x1b[H"));
            startShell();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

}
