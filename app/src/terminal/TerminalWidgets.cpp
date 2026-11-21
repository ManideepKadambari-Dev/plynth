#include "TerminalWidgets.h"

#include "theme/Theme.h"

#include <QMouseEvent>
#include <QPainter>

namespace plynth::app {

namespace {

constexpr int kGripSize = 12;

}

// Makes a flat round button that never takes keyboard focus from the terminal.
TerminalButton::TerminalButton(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

// The button is a circle of the theme size.
QSize TerminalButton::sizeHint() const
{
    const int d = Theme::instance().terminalButton();
    return QSize(d, d);
}

// Draws the Raised disc, the Line border, the >_ mark and a blue ring while the panel is open.
void TerminalButton::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF disc = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    p.setBrush(t.raised());
    QColor border = t.line();
    if (underMouse())
        border = t.muted();
    p.setPen(QPen(border, 1));
    p.drawEllipse(disc);
    if (isChecked()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.signalBlue(), 2));
        p.drawEllipse(disc);
    }
    p.setPen(t.text());
    QFont font = t.monoFont(t.sizeLarge());
    font.setBold(true);
    p.setFont(font);
    p.drawText(rect(), Qt::AlignCenter, QStringLiteral(">_"));
}

// Makes the corner square with a diagonal resize cursor.
TerminalGrip::TerminalGrip(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(kGripSize, kGripSize);
    setCursor(Qt::SizeFDiagCursor);
}

// Starts a resize.
void TerminalGrip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = true;
    startGlobal_ = event->globalPosition().toPoint();
    startSize_ = parentWidget()->size();
}

// Asks for a new panel size while the mouse moves. Dragging up and left makes the panel larger.
void TerminalGrip::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_)
        return;
    const QPoint delta = startGlobal_ - event->globalPosition().toPoint();
    emit sizeRequested(QSize(startSize_.width() + delta.x(), startSize_.height() + delta.y()));
}

// Ends a resize.
void TerminalGrip::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = false;
}

// Builds the frame and its grip.
TerminalPanel::TerminalPanel(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    grip_ = new TerminalGrip(this);
    grip_->setToolTip(tr("Drag to resize the terminal"));
    grip_->setProperty("tourId", QStringLiteral("terminal-resize"));
}

// Keeps the grip in the top left corner above the terminal view.
void TerminalPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    grip_->move(0, 0);
    grip_->raise();
}

// Draws the Panel fill with a Line border and rounded corners.
void TerminalPanel::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(t.panel());
    p.setPen(QPen(t.line(), 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());
}

}
