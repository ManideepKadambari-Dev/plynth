#include "LightTool.h"

#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "tools/ToolController.h"

#include <plynth/core/commands/LightCommands.h>

#include <QKeyEvent>

#include <cmath>

namespace plynth::app {

namespace {

constexpr int kDragStartPixels = 3;

}

// Selects the light under the press and remembers where on it the press was.
bool LightEditor::pressBody(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return false;
    const QString id = host_.canvas().lightAt(mouse.scene);
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (id.isEmpty() || floor == nullptr || !ppm.has_value())
        return false;
    host_.selection().set(SelectionKind::Light, id);
    const plynth::LightMarker* light = plynth::findLight(*floor, id.toStdString());
    if (light == nullptr || host_.document().isReadOnly())
        return true;
    light_ = light->id;
    key_ = host_.nextMergeKey();
    pressView_ = mouse.view;
    grab_ = QPointF(light->x - mouse.scene.x() / *ppm, light->y - mouse.scene.y() / *ppm);
    mode_ = Mode::MovePending;
    return true;
}

// Moves the pressed light once the mouse has travelled a few pixels, snapped to the grid unless Ctrl is held.
void LightEditor::moved(const ToolMouse& mouse)
{
    if (mode_ == Mode::None || !(mouse.buttons & Qt::LeftButton))
        return;
    if (mode_ == Mode::MovePending) {
        const QPoint d = mouse.view - pressView_;
        if (std::abs(d.x()) + std::abs(d.y()) < kDragStartPixels)
            return;
        mode_ = Mode::Move;
    }
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value())
        return;
    const plynth::Vec2 at = host_.snapLight(
        plynth::Vec2{mouse.scene.x() / *ppm + grab_.x(), mouse.scene.y() / *ppm + grab_.y()},
        (mouse.modifiers & Qt::ControlModifier) != 0);
    if (host_.run(std::make_unique<plynth::MoveLightCommand>(light_, at.x, at.y, key_)).ok())
        return;
    mode_ = Mode::None;
}

// Ends the drag. The merge key already made it one undo step.
void LightEditor::released()
{
    mode_ = Mode::None;
}

// Forgets a drag in progress.
void LightEditor::reset()
{
    mode_ = Mode::None;
    light_.clear();
}

// The status bar hint for this tool.
QString LightTool::hint() const
{
    return QObject::tr("Click to place a ceiling light. Drag a light to move it, Delete removes it. Ctrl places freely.");
}

// Drops a drag when another tool takes over.
void LightTool::deactivate()
{
    editor_.reset();
}

// Selects a marker under the click, or places a new ceiling light there.
void LightTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    if (editor_.pressBody(mouse))
        return;
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value())
        return;
    host_.placeLight(plynth::Vec2{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm}, plynth::LightKind::Ceiling,
                     (mouse.modifiers & Qt::ControlModifier) != 0);
}

// Moves the pressed light.
void LightTool::mouseMoved(const ToolMouse& mouse)
{
    editor_.moved(mouse);
}

// Ends a drag.
void LightTool::mouseReleased(const ToolMouse& mouse)
{
    if (mouse.button == Qt::LeftButton)
        editor_.released();
}

// Esc clears the selection.
bool LightTool::keyPressed(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape)
        return false;
    editor_.reset();
    host_.selection().clear();
    return true;
}

// A drag on the old floor ends when another floor is shown.
void LightTool::floorChanged()
{
    editor_.reset();
}

}
