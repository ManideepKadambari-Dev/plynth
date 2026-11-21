#include "SelectTool.h"

#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "tools/ToolController.h"

#include <plynth/core/Snapping.h>
#include <plynth/core/commands/PointCommands.h>

#include <QKeyEvent>

#include <cmath>

namespace plynth::app {

namespace {

constexpr int kDragStartPixels = 3;
int g_nextDragKey = 1;

}

// The status bar hint for this tool.
QString SelectTool::hint() const
{
    return QObject::tr("Click to select. Drag a point to move it. Esc clears the selection.");
}

// Forgets a drag that was in progress.
void SelectTool::deactivate()
{
    dragPoint_.clear();
    dragging_ = false;
}

// Selects what is under the click, or clears the selection. A point may start a drag.
void SelectTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    dragPoint_.clear();
    dragging_ = false;
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value()) {
        host_.selection().clear();
        return;
    }
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    const plynth::Pick pick = plynth::pickAt(*floor, at, host_.snapRadiusMetres());
    switch (pick.kind) {
    case plynth::PickKind::Point:
        host_.selection().set(SelectionKind::Point, QString::fromStdString(pick.id));
        if (!host_.document().isReadOnly()) {
            dragPoint_ = pick.id;
            pressView_ = mouse.view;
            dragKey_ = g_nextDragKey++;
        }
        break;
    case plynth::PickKind::Wall:
        host_.selection().set(SelectionKind::Wall, QString::fromStdString(pick.id));
        break;
    case plynth::PickKind::Room:
        host_.selection().set(SelectionKind::Room, QString::fromStdString(pick.id));
        break;
    case plynth::PickKind::None:
        host_.selection().clear();
        break;
    }
}

// Moves the pressed point once the mouse has travelled a few pixels.
void SelectTool::mouseMoved(const ToolMouse& mouse)
{
    if (dragPoint_.empty() || !(mouse.buttons & Qt::LeftButton))
        return;
    if (!dragging_) {
        const QPoint d = mouse.view - pressView_;
        if (std::abs(d.x()) + std::abs(d.y()) < kDragStartPixels)
            return;
        dragging_ = true;
        if (const plynth::Floor* floor = host_.floor())
            if (const plynth::Point* point = plynth::findPoint(*floor, dragPoint_))
                startMetres_ = QPointF(point->x, point->y);
    }
    dragTo(mouse);
}

// Ends the drag. The merge key already made it one undo step.
void SelectTool::mouseReleased(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    dragPoint_.clear();
    dragging_ = false;
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

}
