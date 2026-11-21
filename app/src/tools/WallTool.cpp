#include "WallTool.h"

#include "canvas/PlanCanvas.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace plynth::app {

// The status bar hint for this tool.
QString WallTool::hint() const
{
    return QObject::tr("Click to place points. Shift keeps it straight. Ctrl places freely. Esc stops.");
}

// Starts with no chain.
void WallTool::activate()
{
    chaining_ = false;
    preview_.reset();
}

// Ends any chain and hides the preview.
void WallTool::deactivate()
{
    chaining_ = false;
    preview_.reset();
    lastScene_.reset();
}

// Turns a cursor position into a snapped position, following the snap rules in core.
plynth::SnapResult WallTool::snapFor(QPointF scene, Qt::KeyboardModifiers modifiers) const
{
    const std::optional<double> ppm = host_.pixelsPerMetre();
    const plynth::Floor* floor = host_.floor();
    const plynth::Vec2 at{scene.x() / *ppm, scene.y() / *ppm};
    plynth::SnapResult free;
    free.position = at;
    if (floor == nullptr || (modifiers & Qt::ControlModifier))
        return free;
    plynth::SnapOptions options;
    options.radius = host_.snapRadiusMetres();
    if (host_.snapToGrid())
        options.gridSize = host_.document().project().settings.gridSize;
    if (chaining_ && (modifiers & Qt::ShiftModifier))
        options.orthogonalFrom = startAt_;
    return plynth::snapPosition(*floor, at, options);
}

// The first click remembers an end. Each next click draws a wall. Right click ends the chain.
void WallTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button == Qt::RightButton) {
        endChain();
        return;
    }
    if (mouse.button != Qt::LeftButton)
        return;
    const plynth::Floor* floor = host_.floor();
    if (floor == nullptr)
        return;
    const plynth::SnapResult snap = snapFor(mouse.scene, mouse.modifiers);
    const plynth::WallEnd end = plynth::wallEndFromSnap(snap);
    if (!chaining_) {
        start_ = end;
        startAt_ = snap.position;
        chainFirstPoint_.clear();
        chainFloor_ = floor->id;
        chaining_ = true;
        refreshPreview();
        return;
    }
    auto command = std::make_unique<plynth::DrawWallCommand>(chainFloor_, start_, end, host_.newWallThickness());
    plynth::DrawWallCommand* raw = command.get();
    running_ = true;
    const plynth::Status status = host_.run(std::move(command));
    running_ = false;
    if (!status.ok())
        return;
    if (chainFirstPoint_.empty())
        chainFirstPoint_ = raw->fromPointId();
    const plynth::Id reached = raw->toPointId();
    const bool closed = end.kind == plynth::WallEnd::Kind::Point && reached == chainFirstPoint_;
    if (closed) {
        endChain();
        return;
    }
    start_ = plynth::WallEnd::atPoint(reached);
    startAt_ = snap.position;
    if (const plynth::Floor* now = host_.floor())
        if (const plynth::Point* point = plynth::findPoint(*now, reached))
            startAt_ = plynth::Vec2{point->x, point->y};
    refreshPreview();
}

// Moves the preview with the cursor.
void WallTool::mouseMoved(const ToolMouse& mouse)
{
    lastScene_ = mouse.scene;
    refreshPreview();
}

// Hides the preview when the cursor leaves the plan.
void WallTool::mouseLeft()
{
    lastScene_.reset();
    preview_.reset();
    host_.canvas().viewport()->update();
}

// Esc ends the chain.
bool WallTool::keyPressed(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        endChain();
        return true;
    }
    return false;
}

// Shift and Control change the snapped position, so the preview is worked out again.
void WallTool::modifiersChanged()
{
    refreshPreview();
}

// A change that this tool did not make (undo, redo, reload) ends the chain.
void WallTool::documentChanged(unsigned changes)
{
    Q_UNUSED(changes);
    if (running_)
        return;
    if (chaining_)
        endChain();
    else
        refreshPreview();
}

// Stops chaining. The next click starts a new chain.
void WallTool::endChain()
{
    chaining_ = false;
    chainFirstPoint_.clear();
    refreshPreview();
}

// Works out the snapped position under the last cursor position and repaints.
void WallTool::refreshPreview()
{
    preview_.reset();
    if (lastScene_.has_value() && host_.pixelsPerMetre().has_value())
        preview_ = snapFor(*lastScene_, QGuiApplication::keyboardModifiers());
    host_.canvas().viewport()->update();
}

// Draws the rubber band and the snap marker in amber.
void WallTool::paint(QPainter& painter)
{
    if (!preview_.has_value())
        return;
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value())
        return;
    const Theme& t = Theme::instance();
    PlanCanvas& canvas = host_.canvas();
    const QPointF at = canvas.sceneToView(QPointF(preview_->position.x * *ppm, preview_->position.y * *ppm));
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (chaining_) {
        const QPointF from = canvas.sceneToView(QPointF(startAt_.x * *ppm, startAt_.y * *ppm));
        const QPointF delta = at - from;
        const double length = std::hypot(delta.x(), delta.y());
        if (length > 0.5) {
            QColor band = t.signalAmber();
            band.setAlpha(t.overlayAlpha());
            const QPointF dir = delta / length;
            const QPointF normal(-dir.y(), dir.x());
            const double half = std::max(1.0, host_.newWallThickness() * *ppm * canvas.zoom() / 2.0);
            const QPointF a = from - dir * half;
            const QPointF b = at + dir * half;
            painter.setPen(Qt::NoPen);
            painter.setBrush(band);
            painter.drawPolygon(QPolygonF({a + normal * half, b + normal * half, b - normal * half, a - normal * half}));
            painter.setPen(QPen(t.signalAmber(), 1));
            painter.drawLine(from, at);
        }
    }

    painter.setPen(QPen(t.signalAmber(), 2));
    painter.setBrush(Qt::NoBrush);
    const double r = t.pointDot() / 2.0;
    switch (preview_->kind) {
    case plynth::SnapKind::Point:
        painter.drawEllipse(at, r + 4, r + 4);
        break;
    case plynth::SnapKind::Wall:
        painter.drawLine(at + QPointF(-r - 2, -r - 2), at + QPointF(r + 2, r + 2));
        painter.drawLine(at + QPointF(-r - 2, r + 2), at + QPointF(r + 2, -r - 2));
        painter.drawEllipse(at, 2, 2);
        break;
    case plynth::SnapKind::Grid:
    case plynth::SnapKind::None:
        painter.drawLine(at + QPointF(-r, 0), at + QPointF(r, 0));
        painter.drawLine(at + QPointF(0, -r), at + QPointF(0, r));
        break;
    }
}

}
