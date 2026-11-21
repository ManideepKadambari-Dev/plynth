#include "SelectTool.h"

#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/Items.h>
#include <plynth/core/Openings.h>
#include <plynth/core/Snapping.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/commands/ItemCommands.h>
#include <plynth/core/commands/OpeningCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <QKeyEvent>
#include <QPainter>

#include <cmath>
#include <numbers>

namespace plynth::app {

namespace {

constexpr int kDragStartPixels = 3;
constexpr double kBendRound = 0.01;

}

// The status bar hint for this tool.
QString SelectTool::hint() const
{
    return QObject::tr("Click to select. Drag a point, opening or item to move it, the round handle of an item to turn it, or the middle handle of a wall to bend it. Esc clears.");
}

// Forgets a drag that was in progress.
void SelectTool::deactivate()
{
    dragPoint_.clear();
    dragOpening_.clear();
    dragItem_.clear();
    rotateItem_.clear();
    bendWall_.clear();
    handleHover_ = false;
    dragging_ = false;
}

// Selects what is under the click, or clears the selection. A point may start a drag, and so may the bend handle.
void SelectTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    dragPoint_.clear();
    dragOpening_.clear();
    dragItem_.clear();
    rotateItem_.clear();
    bendWall_.clear();
    dragging_ = false;
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value()) {
        host_.selection().clear();
        return;
    }
    if (!host_.document().isReadOnly() && host_.selection().kind() == SelectionKind::Wall &&
        overBendHandle(mouse.view)) {
        bendWall_ = host_.selection().id().toStdString();
        dragKey_ = host_.nextMergeKey();
        host_.canvas().viewport()->update();
        return;
    }
    if (!host_.document().isReadOnly() && overRotateHandle(mouse.view)) {
        rotateItem_ = host_.selection().id().toStdString();
        dragKey_ = host_.nextMergeKey();
        host_.canvas().viewport()->update();
        return;
    }
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    host_.selection().setClickAt(at.x, at.y);
    const plynth::Pick pick = plynth::pickAt(*floor, at, host_.snapRadiusMetres());
    switch (pick.kind) {
    case plynth::PickKind::Point:
        host_.selection().set(SelectionKind::Point, QString::fromStdString(pick.id));
        if (!host_.document().isReadOnly()) {
            dragPoint_ = pick.id;
            pressView_ = mouse.view;
            dragKey_ = host_.nextMergeKey();
        }
        break;
    case plynth::PickKind::Wall:
        host_.selection().set(SelectionKind::Wall, QString::fromStdString(pick.id));
        break;
    case plynth::PickKind::Room:
        host_.selection().set(SelectionKind::Room, QString::fromStdString(pick.id));
        break;
    case plynth::PickKind::Opening:
        host_.selection().set(SelectionKind::Opening, QString::fromStdString(pick.id));
        if (!host_.document().isReadOnly()) {
            const plynth::Opening* opening = plynth::findOpening(*floor, pick.id);
            const plynth::Wall* wall = opening ? plynth::findWall(*floor, opening->wall) : nullptr;
            if (wall != nullptr) {
                dragOpening_ = pick.id;
                pressView_ = mouse.view;
                dragKey_ = host_.nextMergeKey();
                openingGrab_ = plynth::projectOntoCurve(at, plynth::wallCurve(*floor, *wall)).offset - opening->offset;
            }
        }
        break;
    case plynth::PickKind::Item:
        host_.selection().set(SelectionKind::Item, QString::fromStdString(pick.id));
        if (!host_.document().isReadOnly()) {
            if (const plynth::Item* item = plynth::findItem(*floor, pick.id)) {
                dragItem_ = pick.id;
                pressView_ = mouse.view;
                dragKey_ = host_.nextMergeKey();
                itemGrab_ = QPointF(item->x - at.x, item->y - at.y);
            }
        }
        break;
    case plynth::PickKind::None:
        host_.selection().clear();
        break;
    }
}

// Moves the pressed point once the mouse has travelled a few pixels, or bends the wall while its handle is held.
void SelectTool::mouseMoved(const ToolMouse& mouse)
{
    if (!bendWall_.empty() && (mouse.buttons & Qt::LeftButton)) {
        bendTo(mouse);
        return;
    }
    if (!rotateItem_.empty() && (mouse.buttons & Qt::LeftButton)) {
        rotateItemTo(mouse);
        return;
    }
    if ((dragPoint_.empty() && dragOpening_.empty() && dragItem_.empty()) || !(mouse.buttons & Qt::LeftButton)) {
        setHandleHover(!host_.document().isReadOnly() &&
                       ((host_.selection().kind() == SelectionKind::Wall && overBendHandle(mouse.view)) ||
                        overRotateHandle(mouse.view)));
        return;
    }
    if (!dragging_) {
        const QPoint d = mouse.view - pressView_;
        if (std::abs(d.x()) + std::abs(d.y()) < kDragStartPixels)
            return;
        dragging_ = true;
        if (const plynth::Floor* floor = host_.floor())
            if (const plynth::Point* point = plynth::findPoint(*floor, dragPoint_))
                startMetres_ = QPointF(point->x, point->y);
    }
    if (!dragItem_.empty())
        moveItemTo(mouse);
    else if (!dragOpening_.empty())
        moveOpeningTo(mouse);
    else
        dragTo(mouse);
}

// Ends the drag. The merge key already made it one undo step.
void SelectTool::mouseReleased(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    dragPoint_.clear();
    dragOpening_.clear();
    dragItem_.clear();
    rotateItem_.clear();
    bendWall_.clear();
    dragging_ = false;
    host_.canvas().viewport()->update();
}

// Hides the hover state of the bend handle when the cursor leaves the plan.
void SelectTool::mouseLeft()
{
    setHandleHover(false);
}

// Esc clears the selection.
bool SelectTool::keyPressed(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        host_.selection().clear();
        return true;
    }
    return false;
}

// Runs MovePointCommand for the cursor position, snapped to the grid and to Shift only.
void SelectTool::dragTo(const ToolMouse& mouse)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value())
        return;
    const plynth::Point* point = plynth::findPoint(*floor, dragPoint_);
    if (point == nullptr)
        return;
    plynth::SnapOptions options;
    options.toPoints = false;
    options.toWalls = false;
    options.radius = 0.0;
    options.ignorePoints = {dragPoint_};
    const bool free = mouse.modifiers & Qt::ControlModifier;
    if (!free && host_.snapToGrid())
        options.gridSize = host_.document().project().settings.gridSize;
    if (!free && (mouse.modifiers & Qt::ShiftModifier))
        options.orthogonalFrom = plynth::Vec2{startMetres_.x(), startMetres_.y()};
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    const plynth::SnapResult snap = plynth::snapPosition(*floor, at, options);
    if (snap.position.x == point->x && snap.position.y == point->y)
        return;
    host_.run(std::make_unique<plynth::MovePointCommand>(dragPoint_, snap.position.x, snap.position.y, dragKey_));
}

// Runs MoveOpeningCommand to slide the pressed opening along its wall to the cursor, in 5 cm steps unless
// Ctrl is held, kept inside the wall. The sill stays.
void SelectTool::moveOpeningTo(const ToolMouse& mouse)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value())
        return;
    const plynth::Opening* opening = plynth::findOpening(*floor, dragOpening_);
    const plynth::Wall* wall = opening ? plynth::findWall(*floor, opening->wall) : nullptr;
    if (wall == nullptr)
        return;
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    double offset = plynth::projectOntoCurve(at, plynth::wallCurve(*floor, *wall)).offset - openingGrab_;
    if (!(mouse.modifiers & Qt::ControlModifier))
        offset = std::round(offset / plynth::kOpeningSnapStep) * plynth::kOpeningSnapStep;
    plynth::Opening moved = *opening;
    moved.offset = offset;
    moved = plynth::fitOpeningBox(*floor, moved);
    if (moved.offset == opening->offset)
        return;
    host_.run(std::make_unique<plynth::MoveOpeningCommand>(dragOpening_, opening->wall, moved.offset, opening->sill,
                                                           dragKey_));
}

// Runs MoveItemCommand for the cursor position. A floor item is snapped to the grid and to wall faces, Ctrl
// moves it freely. A wall item is given the plain position, and core slides it along its wall.
void SelectTool::moveItemTo(const ToolMouse& mouse)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value())
        return;
    const plynth::Item* item = plynth::findItem(*floor, dragItem_);
    if (item == nullptr)
        return;
    plynth::Vec2 wanted{mouse.scene.x() / *ppm + itemGrab_.x(), mouse.scene.y() / *ppm + itemGrab_.y()};
    if (!item->wall.has_value())
        wanted = host_.snapItem(*item, wanted, mouse.modifiers & Qt::ControlModifier);
    if (wanted.x == item->x && wanted.y == item->y)
        return;
    host_.run(std::make_unique<plynth::MoveItemCommand>(dragItem_, wanted, dragKey_));
}

// Runs RotateItemCommand so the item's front points at the cursor, in 15 degree steps unless Shift is held.
void SelectTool::rotateItemTo(const ToolMouse& mouse)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value())
        return;
    const plynth::Item* item = plynth::findItem(*floor, rotateItem_);
    if (item == nullptr)
        return;
    const double dx = mouse.scene.x() / *ppm - item->x;
    const double dy = mouse.scene.y() / *ppm - item->y;
    if (std::hypot(dx, dy) < 1e-6)
        return;
    double degrees = std::atan2(-dx, dy) * 180.0 / std::numbers::pi;
    if (!(mouse.modifiers & Qt::ShiftModifier))
        degrees = std::round(degrees / plynth::kItemRotationStep) * plynth::kItemRotationStep;
    degrees = plynth::normalizeRotation(degrees);
    if (degrees == item->rotation)
        return;
    host_.run(std::make_unique<plynth::RotateItemCommand>(rotateItem_, degrees, dragKey_));
    host_.canvas().viewport()->update();
}

// Where the rotation handle of the selected floor item is in canvas pixels: beyond the middle of its front edge.
std::optional<QPointF> SelectTool::rotateHandleView() const
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value() || host_.selection().kind() != SelectionKind::Item)
        return std::nullopt;
    const plynth::Item* item = plynth::findItem(*floor, host_.selection().id().toStdString());
    if (item == nullptr || item->wall.has_value())
        return std::nullopt;
    const plynth::ItemFootprint footprint = plynth::itemFootprint(*item);
    const plynth::Vec2 mid{(footprint.corners[2].x + footprint.corners[3].x) / 2.0,
                           (footprint.corners[2].y + footprint.corners[3].y) / 2.0};
    const QPointF edge = host_.canvas().sceneToView(QPointF(mid.x * *ppm, mid.y * *ppm));
    const QPointF centre = host_.canvas().sceneToView(QPointF(item->x * *ppm, item->y * *ppm));
    const QPointF away = edge - centre;
    const double length = std::hypot(away.x(), away.y());
    if (length < 1.0)
        return std::nullopt;
    return edge + away / length * (Theme::instance().rotationHandle() * 2.0);
}

// Says whether a canvas position is on the rotation handle of the selected item.
bool SelectTool::overRotateHandle(const QPoint& view) const
{
    const std::optional<QPointF> at = rotateHandleView();
    if (!at.has_value())
        return false;
    const QPointF d = QPointF(view) - *at;
    return std::hypot(d.x(), d.y()) <= Theme::instance().rotationHandle() / 2.0 + 3.0;
}

// Runs SetWallBulgeCommand for the cursor position. The wall goes straight when the cursor is within the snap
// radius of the line between its points, and the bend is rounded to 0.01 m unless Ctrl is held.
void SelectTool::bendTo(const ToolMouse& mouse)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value())
        return;
    const plynth::Wall* wall = plynth::findWall(*floor, bendWall_);
    if (wall == nullptr)
        return;
    const plynth::WallCurve curve = plynth::wallCurve(*floor, *wall);
    if (!curve.valid)
        return;
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    double bulge = plynth::bulgeFromPosition(curve.from, curve.to, at);
    if (std::abs(bulge) <= host_.snapRadiusMetres())
        bulge = 0.0;
    else if (!(mouse.modifiers & Qt::ControlModifier))
        bulge = std::round(bulge / kBendRound) * kBendRound;
    if (bulge == wall->bulge)
        return;
    host_.run(std::make_unique<plynth::SetWallBulgeCommand>(bendWall_, bulge, dragKey_));
    host_.canvas().viewport()->update();
}

// Where the bend handle of the selected wall is in canvas pixels: the middle of its curve.
std::optional<QPointF> SelectTool::bendHandleView() const
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value() || host_.selection().kind() != SelectionKind::Wall)
        return std::nullopt;
    const plynth::Wall* wall = plynth::findWall(*floor, host_.selection().id().toStdString());
    if (wall == nullptr)
        return std::nullopt;
    const plynth::WallCurve curve = plynth::wallCurve(*floor, *wall);
    if (!curve.valid)
        return std::nullopt;
    const plynth::Vec2 middle = plynth::pointAlong(curve, curve.length / 2.0);
    return host_.canvas().sceneToView(QPointF(middle.x * *ppm, middle.y * *ppm));
}

// Says whether a canvas position is on the bend handle of the selected wall.
bool SelectTool::overBendHandle(const QPoint& view) const
{
    const std::optional<QPointF> at = bendHandleView();
    if (!at.has_value())
        return false;
    const double reach = Theme::instance().bendHandle() / 2.0 + 3.0;
    const QPointF d = QPointF(view) - *at;
    return std::hypot(d.x(), d.y()) <= reach;
}

// Remembers whether the cursor is over the bend handle and repaints when that changes.
void SelectTool::setHandleHover(bool hover)
{
    if (handleHover_ == hover)
        return;
    handleHover_ = hover;
    host_.canvas().viewport()->update();
}

// Draws the bend handle of the selected wall: a blue ring, amber while hovered or dragged.
void SelectTool::paint(QPainter& painter)
{
    if (host_.document().isReadOnly())
        return;
    if (const std::optional<QPointF> turn = rotateHandleView()) {
        const Theme& t = Theme::instance();
        const bool active = handleHover_ || !rotateItem_.empty();
        const plynth::Floor* floor = host_.floor();
        const plynth::Item* item = floor ? plynth::findItem(*floor, host_.selection().id().toStdString()) : nullptr;
        const std::optional<double> ppm = host_.pixelsPerMetre();
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (item != nullptr && ppm.has_value()) {
            const plynth::ItemFootprint footprint = plynth::itemFootprint(*item);
            const QPointF edge = host_.canvas().sceneToView(
                QPointF((footprint.corners[2].x + footprint.corners[3].x) / 2.0 * *ppm,
                        (footprint.corners[2].y + footprint.corners[3].y) / 2.0 * *ppm));
            painter.setPen(QPen(t.signalAmber(), 1.5));
            painter.drawLine(edge, *turn);
        }
        painter.setPen(QPen(active ? t.signalAmber() : t.signalBlue(), 2));
        painter.setBrush(t.ground());
        painter.drawEllipse(*turn, t.rotationHandle() / 2.0, t.rotationHandle() / 2.0);
    }
    const std::optional<QPointF> at = bendHandleView();
    if (!at.has_value())
        return;
    const Theme& t = Theme::instance();
    const bool active = handleHover_ || !bendWall_.empty();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(active ? t.signalAmber() : t.signalBlue(), 2));
    painter.setBrush(t.ground());
    painter.drawEllipse(*at, t.bendHandle() / 2.0, t.bendHandle() / 2.0);
}

}
