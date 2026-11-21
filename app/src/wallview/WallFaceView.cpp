#include "WallFaceView.h"

#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <QApplication>
#include <QFontMetrics>
#include <QGraphicsScene>

#include <plynth/core/Openings.h>
#include <plynth/core/commands/OpeningCommands.h>

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
WallFaceView::WallFaceView(ToolController* host, QWidget* parent)
    : QGraphicsView(parent), host_(host)
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
    const double top = margin;
    const double bottom = viewport()->height() - margin;
    const double availH = std::max(1.0, bottom - top);
    scale_ = std::clamp(std::min(availW / contentW, availH / contentH), kMinScale, kMaxScale);
    const double left = margin + kRulerWidth + (availW - contentW * scale_) / 2.0;
    const double floorY = top + availH - (availH - contentH * scale_) / 2.0;
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
    paintMeasures(painter);
    paintHandles(painter);
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

// True when the project can be edited and there is a host to run commands through.
bool WallFaceView::editable() const
{
    return host_ != nullptr && !host_->document().isReadOnly();
}

// The selected opening of the wall shown, or null.
const WallViewOpening* WallFaceView::selectedOpening() const
{
    for (const WallViewOpening& opening : data_.openings)
        if (opening.id == data_.selectedOpening)
            return &opening;
    return nullptr;
}

// The opening whose outline holds a place on the face, the later ones first. Null when there is none.
const WallViewOpening* WallFaceView::openingAt(const QPointF& face) const
{
    for (qsizetype i = data_.openings.size(); i-- > 0;)
        if (data_.openings[i].outline.containsPoint(face, Qt::OddEvenFill))
            return &data_.openings[i];
    return nullptr;
}

// Where a handle is, in canvas pixels. Handles run around the box as seen: 0 top left, 1 top, 2 top right,
// 3 right, 4 bottom right, 5 bottom, 6 bottom left, 7 left.
QPointF WallFaceView::handlePosition(const Box& box, int handle) const
{
    const QPointF a = toView(box.u0, box.v1);
    const QPointF b = toView(box.u1, box.v0);
    const double left = std::min(a.x(), b.x());
    const double right = std::max(a.x(), b.x());
    const double top = std::min(a.y(), b.y());
    const double bottom = std::max(a.y(), b.y());
    const double midX = (left + right) / 2.0;
    const double midY = (top + bottom) / 2.0;
    switch (handle) {
    case 0: return {left, top};
    case 1: return {midX, top};
    case 2: return {right, top};
    case 3: return {right, midY};
    case 4: return {right, bottom};
    case 5: return {midX, bottom};
    case 6: return {left, bottom};
    default: return {left, midY};
    }
}

// The handle of the selected opening under a canvas position, or -1.
int WallFaceView::handleAt(const QPoint& view) const
{
    const WallViewOpening* opening = selectedOpening();
    if (opening == nullptr || !editable())
        return -1;
    const Box box{opening->offset - opening->width / 2.0, opening->offset + opening->width / 2.0, opening->sill,
                  opening->sill + opening->height};
    const double reach = Theme::instance().handleSize() / 2.0 + 3.0;
    for (int handle = 0; handle < 8; ++handle) {
        const QPointF d = QPointF(view) - handlePosition(box, handle);
        if (std::abs(d.x()) <= reach && std::abs(d.y()) <= reach)
            return handle;
    }
    return -1;
}

// Sets the cursor and the hovered handle for a pointer position.
void WallFaceView::updateHover(const QPoint& view)
{
    const int handle = handleAt(view);
    if (handle != hoverHandle_) {
        hoverHandle_ = handle;
        viewport()->update();
    }
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (handle >= 0) {
        static const Qt::CursorShape shapes[8] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
                                                  Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                                  Qt::SizeBDiagCursor, Qt::SizeHorCursor};
        shape = shapes[handle];
    } else if (editable() && host_->active() == ToolId::Opening) {
        shape = Qt::CrossCursor;
    } else if (editable() && openingAt(toFace(view)) != nullptr) {
        shape = Qt::SizeAllCursor;
    }
    viewport()->setCursor(shape);
}

// Adds an opening of the Opening tool at a place on the face, at the default sill, and selects it.
void WallFaceView::addOpeningAt(const QPointF& face, Qt::KeyboardModifiers modifiers)
{
    if (!data_.valid || face.x() < 0.0 || face.x() > data_.length)
        return;
    double offset = face.x();
    if (!(modifiers & Qt::ControlModifier))
        offset = std::round(offset / plynth::kOpeningSnapStep) * plynth::kOpeningSnapStep;
    plynth::Opening opening = host_->newOpening(data_.wallId.toStdString(), offset);
    if (const plynth::Floor* floor = host_->floor())
        opening = plynth::fitOpeningBox(*floor, opening);
    auto command = std::make_unique<plynth::AddOpeningCommand>(opening);
    const plynth::AddOpeningCommand* raw = command.get();
    if (host_->run(std::move(command)).ok())
        host_->selection().set(SelectionKind::Opening, QString::fromStdString(raw->openingId()));
}

// Moves or resizes the dragged opening to the pointer. Positions snap to 5 cm unless Ctrl is held, and the
// box stays inside the free part of the wall and under its top.
void WallFaceView::dragTo(const QPoint& view, Qt::KeyboardModifiers modifiers)
{
    const plynth::Floor* floor = host_->floor();
    const plynth::Opening* current = floor ? plynth::findOpening(*floor, dragId_) : nullptr;
    if (current == nullptr)
        return;
    const bool free = modifiers & Qt::ControlModifier;
    const auto snap = [free](double value) {
        return free ? value : std::round(value / plynth::kOpeningSnapStep) * plynth::kOpeningSnapStep;
    };
    const QPointF face = toFace(QPointF(view));
    Box box = dragStartBox_;

    if (drag_ == Drag::Move) {
        double du = face.x() - dragStartFace_.x();
        double dv = face.y() - dragStartFace_.y();
        if (modifiers & Qt::ShiftModifier) {
            const QPoint moved = view - dragStartView_;
            if (std::abs(moved.x()) >= std::abs(moved.y()))
                dv = 0.0;
            else
                du = 0.0;
        }
        const double centre = snap((dragStartBox_.u0 + dragStartBox_.u1) / 2.0 + du);
        const double sill = snap(dragStartBox_.v0 + dv);
        plynth::Opening moved = *current;
        moved.offset = centre;
        moved.sill = std::max(0.0, sill);
        moved = plynth::fitOpeningBox(*floor, moved);
        if (moved.offset == current->offset && moved.sill == current->sill)
            return;
        host_->run(std::make_unique<plynth::MoveOpeningCommand>(dragId_, current->wall, moved.offset, moved.sill,
                                                                dragKey_));
        return;
    }

    // Resize: handles are in the order of the view, so the left and right ones swap when the face is mirrored.
    const bool viewLeft = dragHandle_ == 0 || dragHandle_ == 6 || dragHandle_ == 7;
    const bool viewRight = dragHandle_ == 2 || dragHandle_ == 3 || dragHandle_ == 4;
    const bool top = dragHandle_ == 0 || dragHandle_ == 1 || dragHandle_ == 2;
    const bool bottom = dragHandle_ == 4 || dragHandle_ == 5 || dragHandle_ == 6;
    const bool lowU = data_.seenFromRight ? viewLeft : viewRight;
    const bool highU = data_.seenFromRight ? viewRight : viewLeft;
    const double spanLow = data_.hasSpan ? std::max(0.0, data_.spanStart) : 0.0;
    const double spanHigh = data_.hasSpan ? std::min(data_.length, data_.spanEnd) : data_.length;
    constexpr double kMinSize = 0.1;
    if (lowU)
        box.u0 = std::clamp(snap(face.x()), spanLow, box.u1 - kMinSize);
    if (highU)
        box.u1 = std::clamp(snap(face.x()), box.u0 + kMinSize, spanHigh);
    if (bottom)
        box.v0 = std::clamp(snap(face.y()), 0.0, box.v1 - kMinSize);
    if (top)
        box.v1 = std::clamp(snap(face.y()), box.v0 + kMinSize, data_.height);
    const double offset = (box.u0 + box.u1) / 2.0;
    const double width = box.u1 - box.u0;
    const double height = box.v1 - box.v0;
    if (offset == current->offset && width == current->width && box.v0 == current->sill && height == current->height)
        return;
    host_->run(std::make_unique<plynth::ResizeOpeningCommand>(dragId_, offset, width, box.v0, height, dragKey_));
}

// Draws the eight handles around the selected opening.
void WallFaceView::paintHandles(QPainter& painter)
{
    const WallViewOpening* opening = selectedOpening();
    if (opening == nullptr || !editable())
        return;
    const Theme& t = Theme::instance();
    const Box box{opening->offset - opening->width / 2.0, opening->offset + opening->width / 2.0, opening->sill,
                  opening->sill + opening->height};
    const double half = t.handleSize() / 2.0;
    for (int handle = 0; handle < 8; ++handle) {
        const QPointF at = handlePosition(box, handle);
        const bool active = handle == hoverHandle_ || (drag_ == Drag::Resize && handle == dragHandle_);
        painter.setPen(QPen(active ? t.signalAmber() : t.signalBlue(), 1.5));
        painter.setBrush(t.ground());
        painter.drawRect(QRectF(at.x() - half, at.y() - half, half * 2.0, half * 2.0));
    }
}

// Writes the size, the sill and the distance from the free end of the selected opening in mono.
void WallFaceView::paintMeasures(QPainter& painter)
{
    const WallViewOpening* opening = selectedOpening();
    if (opening == nullptr)
        return;
    const Theme& t = Theme::instance();
    painter.setFont(t.monoFont(t.sizeSmall()));
    const QFontMetrics metrics(painter.font());
    const auto label = [&](const QPointF& centre, const QString& text) {
        const QRectF box(centre.x() - metrics.horizontalAdvance(text) / 2.0 - 4.0, centre.y() - metrics.height() / 2.0 - 1.0,
                         metrics.horizontalAdvance(text) + 8.0, metrics.height() + 2.0);
        QColor backing = t.panel();
        backing.setAlpha(230);
        painter.setPen(Qt::NoPen);
        painter.setBrush(backing);
        painter.drawRoundedRect(box, t.radius(), t.radius());
        painter.setPen(t.text());
        painter.drawText(box, Qt::AlignCenter, text);
    };
    const double u0 = opening->offset - opening->width / 2.0;
    const double u1 = opening->offset + opening->width / 2.0;
    const double v1 = opening->sill + opening->height;
    const QPointF topMiddle = toView(opening->offset, v1);
    label(QPointF(topMiddle.x(), topMiddle.y() - metrics.height()),
          tr("%1 x %2 m").arg(opening->width, 0, 'f', 2).arg(opening->height, 0, 'f', 2));

    if (opening->sill > 1e-6) {
        const QPointF at = toView(opening->offset, opening->sill / 2.0);
        painter.setPen(QPen(t.muted(), 1.0, Qt::DashLine));
        painter.drawLine(toView(opening->offset, 0.0), toView(opening->offset, opening->sill));
        label(at, tr("sill %1 m").arg(opening->sill, 0, 'f', 2));
    }

    if (data_.hasSpan) {
        // Distance from the left end of the free part, as seen, to the left edge of the opening.
        const double freeLeft = data_.seenFromRight ? data_.spanStart : data_.length - data_.spanEnd;
        const double openLeft = data_.seenFromRight ? u0 : data_.length - u1;
        const double y = origin_.y() + 12.0;
        const QPointF a(origin_.x() + freeLeft * scale_, y);
        const QPointF b(origin_.x() + openLeft * scale_, y);
        painter.setPen(QPen(t.muted(), 1.0));
        painter.drawLine(a, b);
        painter.drawLine(QPointF(a.x(), y - 3.0), QPointF(a.x(), y + 3.0));
        painter.drawLine(QPointF(b.x(), y - 3.0), QPointF(b.x(), y + 3.0));
        label(QPointF((a.x() + b.x()) / 2.0, y), tr("%1 m").arg(std::abs(openLeft - freeLeft), 0, 'f', 2));
    }
}

// Starts a pan on the middle button. The left button starts a resize on a handle, a move on an opening, or
// adds an opening while the Opening tool is active.
void WallFaceView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastPan_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && data_.valid && host_ != nullptr) {
        const QPoint view = event->pos();
        const QPointF face = toFace(QPointF(view));
        const int handle = handleAt(view);
        if (handle >= 0) {
            if (const WallViewOpening* opening = selectedOpening()) {
                drag_ = Drag::Resize;
                dragHandle_ = handle;
                dragId_ = opening->id.toStdString();
                dragStartBox_ = {opening->offset - opening->width / 2.0, opening->offset + opening->width / 2.0,
                                 opening->sill, opening->sill + opening->height};
                dragKey_ = host_->nextMergeKey();
            }
        } else if (editable() && host_->active() == ToolId::Opening) {
            addOpeningAt(face, event->modifiers());
        } else if (const WallViewOpening* opening = openingAt(face)) {
            host_->selection().set(SelectionKind::Opening, opening->id);
            if (editable()) {
                drag_ = Drag::Move;
                dragId_ = opening->id.toStdString();
                dragStartBox_ = {opening->offset - opening->width / 2.0, opening->offset + opening->width / 2.0,
                                 opening->sill, opening->sill + opening->height};
                dragStartFace_ = face;
                dragStartView_ = view;
                dragKey_ = host_->nextMergeKey();
            }
        }
        viewport()->update();
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

// Moves the face while a pan is in progress, drags the opening while the left button is held, and otherwise
// updates the hover.
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
    if (drag_ != Drag::None && (event->buttons() & Qt::LeftButton)) {
        dragTo(event->pos(), event->modifiers());
        event->accept();
        return;
    }
    if (data_.valid && host_ != nullptr)
        updateHover(event->pos());
    QGraphicsView::mouseMoveEvent(event);
}

// Ends a pan or a drag. The merge key already made the drag one undo step.
void WallFaceView::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_ && event->button() == Qt::MiddleButton) {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    if (drag_ != Drag::None && event->button() == Qt::LeftButton) {
        drag_ = Drag::None;
        dragHandle_ = -1;
        updateHover(event->pos());
        viewport()->update();
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
    if (host_ != nullptr && data_.valid)
        host_->showNotice(tr("Drag an opening to move it, drag its edges to resize. Shift keeps it level. Ctrl moves freely."));
    if (qobject_cast<QLineEdit*>(QApplication::focusWidget()) == nullptr)
        setFocus(Qt::MouseFocusReason);
}

// Drops the hover state when the pointer leaves.
void WallFaceView::leaveEvent(QEvent* event)
{
    QGraphicsView::leaveEvent(event);
    if (hoverHandle_ != -1) {
        hoverHandle_ = -1;
        viewport()->update();
    }
}

}
