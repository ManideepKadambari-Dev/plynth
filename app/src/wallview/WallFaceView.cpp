#include "WallFaceView.h"

#include "theme/Theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace plynth::app {

namespace {

constexpr double kMinScale = 3.0;
constexpr double kMaxScale = 3000.0;
constexpr double kStep = 1.25;
constexpr double kNotch = 120.0;
constexpr int kRulerWidth = 44;
constexpr double kLevelTolerance = 1e-6;

}

// Sets up an empty scene (the face is painted directly), hides the scroll bars and turns on mouse tracking.
WallFaceView::WallFaceView(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setToolTip(tr("Wall face at true scale. Wheel zooms at the cursor. Middle drag pans. Home fits the wall."));
}

// Keeps the new data. A different wall, or fit mode, fits the face in the view again.
void WallFaceView::setData(const WallViewData& data)
{
    const bool otherWall = data.wallId != data_.wallId;
    data_ = data;
    if (data_.valid && (fitMode_ || otherWall)) {
        fitMode_ = true;
        applyFit();
    }
    viewport()->update();
}

// Fits the whole face and keeps it fitted while the view is resized.
void WallFaceView::fit()
{
    fitMode_ = true;
    applyFit();
    viewport()->update();
}

// Works out the scale that shows the face with its margin and ruler, and centres it.
void WallFaceView::applyFit()
{
    if (!data_.valid || data_.length <= 0.0)
        return;
    const int margin = Theme::instance().wallViewMargin();
    const double contentW = data_.length;
    const double contentH = std::max({data_.height, data_.ceilingHeight, 0.1});
    const double availW = std::max(1, viewport()->width() - 2 * margin - kRulerWidth);
    const double availH = std::max(1, viewport()->height() - 2 * margin);
    scale_ = std::clamp(std::min(availW / contentW, availH / contentH), kMinScale, kMaxScale);
    const double left = margin + kRulerWidth + (availW - contentW * scale_) / 2.0;
    const double floorY = margin + availH - (availH - contentH * scale_) / 2.0;
    origin_ = QPointF(left, floorY);
}

// The canvas position of a place on the face. u is mirrored when the face is seen from the left side.
QPointF WallFaceView::toView(double u, double v) const
{
    const double shown = data_.seenFromRight ? u : data_.length - u;
    return QPointF(origin_.x() + shown * scale_, origin_.y() - v * scale_);
}

// The place on the face under a canvas position.
QPointF WallFaceView::toFace(const QPointF& view) const
{
    const double shown = (view.x() - origin_.x()) / scale_;
    return QPointF(data_.seenFromRight ? shown : data_.length - shown, (origin_.y() - view.y()) / scale_);
}

// Paints the Ground background and then the face, ruler, openings and notes.
void WallFaceView::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), t.ground());
    if (!data_.valid)
        return;
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintFace(painter);
    paintOpenings(painter);
    paintRuler(painter);
    paintNotes(painter);
}

// Paints the wall face in Raised with its Signal blue outline, the shaded parts outside the free span, the
// dashed ceiling line and the floor line.
void WallFaceView::paintFace(QPainter& painter)
{
    const Theme& t = Theme::instance();
    const QRectF face = QRectF(toView(0.0, data_.height), toView(data_.length, 0.0)).normalized();
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.raised());
    painter.drawRect(face);

    if (data_.hasSpan) {
        painter.setBrush(t.line());
        const double a = std::clamp(data_.spanStart, 0.0, data_.length);
        const double b = std::clamp(data_.spanEnd, 0.0, data_.length);
        if (b < a) {
            painter.drawRect(face);
        } else {
            if (a > 0.0)
                painter.drawRect(QRectF(toView(0.0, data_.height), toView(a, 0.0)).normalized());
            if (b < data_.length)
                painter.drawRect(QRectF(toView(b, data_.height), toView(data_.length, 0.0)).normalized());
        }
    }

    if (std::abs(data_.height - data_.ceilingHeight) > kLevelTolerance) {
        QPen dashed(t.muted(), 1.0);
        dashed.setDashPattern({4.0, 4.0});
        painter.setPen(dashed);
        const QPointF left = toView(0.0, data_.ceilingHeight);
        const QPointF right = toView(data_.length, data_.ceilingHeight);
        const double xa = std::min(left.x(), right.x()) - 10.0;
        const double xb = std::max(left.x(), right.x()) + 10.0;
        painter.drawLine(QPointF(xa, left.y()), QPointF(xb, left.y()));
        painter.setFont(t.monoFont(t.sizeSmall()));
        painter.drawText(QPointF(xa, left.y() - 4.0), tr("ceiling %1 m").arg(data_.ceilingHeight, 0, 'f', 2));
    }

    painter.setPen(QPen(t.signalBlue(), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(face);

    painter.setPen(QPen(t.muted(), 2.0));
    const QPointF floorLeft = toView(0.0, 0.0);
    const QPointF floorRight = toView(data_.length, 0.0);
    painter.drawLine(QPointF(std::min(floorLeft.x(), floorRight.x()) - 10.0, floorLeft.y()),
                     QPointF(std::max(floorLeft.x(), floorRight.x()) + 10.0, floorLeft.y()));
}

// Paints the openings as holes in the face. The selected one is amber, one that does not fit is dashed amber.
void WallFaceView::paintOpenings(QPainter& painter)
{
    const Theme& t = Theme::instance();
    for (const WallViewOpening& opening : data_.openings) {
        const bool selected = opening.id == data_.selectedOpening;
        QPolygonF shape;
        for (const QPointF& corner : opening.outline)
            shape << toView(corner.x(), corner.y());
        QPen pen(selected || opening.problem ? t.signalAmber() : t.signalBlue(), selected ? 2.0 : 1.5);
        if (opening.problem)
            pen.setDashPattern({4.0, 3.0});
        painter.setPen(pen);
        painter.setBrush(t.ground());
        painter.drawPolygon(shape);
    }
    for (const WallViewItem& item : data_.items) {
        QPolygonF shape;
        for (const QPointF& corner : item.outline)
            shape << toView(corner.x(), corner.y());
        painter.setPen(QPen(item.selected ? t.signalAmber() : t.signalBlue(), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(shape);
    }
}

// Paints the height ruler at the left of the face: a tick every 0.5 m, a label every 1 m.
void WallFaceView::paintRuler(QPainter& painter)
{
    const Theme& t = Theme::instance();
    const double top = std::max({data_.height, data_.ceilingHeight});
    const double x = origin_.x() - 14.0;
    painter.setPen(QPen(t.muted(), 1.0));
    painter.setFont(t.monoFont(t.sizeSmall()));
    const QFontMetrics metrics(painter.font());
    for (int half = 0; half * 0.5 <= top + 1e-9; ++half) {
        const double v = half * 0.5;
        const double y = origin_.y() - v * scale_;
        const bool whole = half % 2 == 0;
        painter.drawLine(QPointF(x, y), QPointF(x + (whole ? 8.0 : 4.0), y));
        if (whole) {
            const QString label = tr("%1 m").arg(half / 2);
            painter.drawText(QPointF(x - 4.0 - metrics.horizontalAdvance(label), y + metrics.ascent() / 2.0 - 1.0),
                             label);
        }
    }
    painter.drawLine(QPointF(x + 8.0, origin_.y()), QPointF(x + 8.0, origin_.y() - top * scale_));
}

// Paints the note for a curved wall.
void WallFaceView::paintNotes(QPainter& painter)
{
    if (!data_.curved)
        return;
    const Theme& t = Theme::instance();
    painter.setPen(t.muted());
    painter.setFont(t.uiFont(t.sizeSmall()));
    painter.drawText(QRectF(t.unit() * 2, t.unit() * 2, viewport()->width() - t.unit() * 4, t.sizeSmall() * 2.0),
                     Qt::AlignLeft | Qt::AlignTop, tr("Curved wall, shown unrolled"));
}

// Zooms at the cursor. One notch is 1.25 times.
void WallFaceView::wheelEvent(QWheelEvent* event)
{
    const double delta = event->angleDelta().y();
    if (!data_.valid || delta == 0.0) {
        event->ignore();
        return;
    }
    const double next = std::clamp(scale_ * std::pow(kStep, delta / kNotch), kMinScale, kMaxScale);
    const QPointF anchor = event->position();
    origin_ = anchor + (origin_ - anchor) * (next / scale_);
    scale_ = next;
    fitMode_ = false;
    viewport()->update();
    event->accept();
}

// Starts a pan on the middle button.
void WallFaceView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastPan_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

// Moves the face while a pan is in progress.
void WallFaceView::mouseMoveEvent(QMouseEvent* event)
{
    if (panning_) {
        const QPoint d = event->pos() - lastPan_;
        lastPan_ = event->pos();
        if (!d.isNull()) {
            fitMode_ = false;
            origin_ += QPointF(d);
            viewport()->update();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

// Ends a pan.
void WallFaceView::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_ && event->button() == Qt::MiddleButton) {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

// Home fits the whole face.
void WallFaceView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Home) {
        fit();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

// Keeps the face fitted after a resize while the user has not zoomed or panned.
void WallFaceView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (fitMode_) {
        applyFit();
        viewport()->update();
    }
}

// Fits once the real size is known.
void WallFaceView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    if (fitMode_)
        applyFit();
}

// Takes keyboard focus when the mouse comes in, so Home works without a click.
void WallFaceView::enterEvent(QEnterEvent* event)
{
    QGraphicsView::enterEvent(event);
    if (qobject_cast<QLineEdit*>(QApplication::focusWidget()) == nullptr)
        setFocus(Qt::MouseFocusReason);
}

}
