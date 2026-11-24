#include "TourOverlay.h"

#include "TourCard.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace plynth::guide {

// Covers the window, follows its size and builds the card.
TourOverlay::TourOverlay(const GuideStyle& style, QWidget* window) : QWidget(window), style_(style)
{
    setProperty("tourId", QStringLiteral("tour-layer"));
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    card_ = new TourCard(style, true, this);
    connect(card_, &TourCard::backClicked, this, &TourOverlay::backRequested);
    connect(card_, &TourCard::nextClicked, this, &TourOverlay::nextRequested);
    connect(card_, &TourCard::skipClicked, this, &TourOverlay::skipRequested);
    window->installEventFilter(this);
    updateLayout();
    raise();
}

// Remembers the control and places the card beside it.
void TourOverlay::setTarget(QWidget* control)
{
    target_ = control;
    updateLayout();
}

// Fits the layer to the window and puts the card where it does not cover the target.
void TourOverlay::updateLayout()
{
    if (parentWidget()) setGeometry(parentWidget()->rect());
    const QRect target = targetRect();
    const QSize size = card_->cardSize();
    const QRect anchor = target.isEmpty() ? QRect(rect().center(), QSize(1, 1)) : target;
    card_->setGeometry(TourCard::placeCard(rect(), anchor, size, style_.unit * 3));
    raise();
    update();
}

// The control's rectangle, a little larger so the outline does not touch it.
QRect TourOverlay::targetRect() const
{
    if (!target_ || !target_->isVisible()) return {};
    const QPoint topLeft = mapFromGlobal(target_->mapToGlobal(QPoint(0, 0)));
    const int margin = style_.unit;
    return QRect(topLeft, target_->size()).adjusted(-margin, -margin, margin, margin).intersected(rect());
}

// Dims all but the target and draws the amber outline around it.
void TourOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor dim = style_.ground;
    dim.setAlpha(style_.dimAlpha);
    QPainterPath shade;
    shade.addRect(rect());
    const QRect target = targetRect();
    if (!target.isEmpty()) {
        QPainterPath hole;
        hole.addRoundedRect(target, style_.radius, style_.radius);
        shade = shade.subtracted(hole);
    }
    painter.fillPath(shade, dim);
    if (!target.isEmpty()) {
        QPen pen(style_.amber);
        pen.setWidth(style_.outlineWidth);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(target, style_.radius, style_.radius);
    }
}

// Esc skips, Left goes back, Right and Enter go on.
void TourOverlay::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape: emit skipRequested(); return;
    case Qt::Key_Left: emit backRequested(); return;
    case Qt::Key_Right:
    case Qt::Key_Return:
    case Qt::Key_Enter: emit nextRequested(); return;
    default: event->accept(); return;
    }
}

// A click outside the card does nothing, so the app under the layer is never used by mistake.
void TourOverlay::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

// Follows the window when it is resized or shown again.
bool TourOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        updateLayout();
    }
    return QWidget::eventFilter(watched, event);
}

}
