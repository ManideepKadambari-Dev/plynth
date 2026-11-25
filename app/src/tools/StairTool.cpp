#include "StairTool.h"

#include "canvas/OverlayBuilder.h"
#include "canvas/PlanCanvas.h"
#include "canvas/StairPaint.h"
#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/Stairs.h>
#include <plynth/core/commands/StairCommands.h>

#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>

#include <cmath>
#include <numbers>

namespace plynth::app {

namespace {

constexpr int kDragStartPixels = 3;
constexpr double kTurnStep = 15.0;
constexpr double kMinDragMetres = 0.3;

// The rotation of a stair whose climb points along a step: 0 climbs toward +y, turned clockwise.
double rotationToward(double dx, double dy)
{
    return std::atan2(-dx, dy) * 180.0 / std::numbers::pi;
}

// Turns a rotation into the range from 0 up to 360.
double wrapRotation(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0)
        degrees += 360.0;
    return degrees >= 360.0 ? 0.0 : degrees;
}

// Rounds a place to the grid, or leaves it when the grid is off or Ctrl is held.
plynth::Vec2 snapToGrid(const ToolController& host, plynth::Vec2 at, bool free)
{
    if (free || !host.snapToGrid())
        return at;
    const double step = host.document().project().settings.gridSize;
    if (step <= 0.0)
        return at;
    return plynth::Vec2{std::round(at.x / step) * step, std::round(at.y / step) * step};
}

}

// The selected stair when it is on the shown floor and the project can be edited.
const plynth::Stair* StairEditor::selected() const
{
    const plynth::Floor* floor = host_.floor();
    if (floor == nullptr || host_.document().isReadOnly() || host_.selection().kind() != SelectionKind::Stair)
        return nullptr;
    return plynth::findStair(*floor, host_.selection().id().toStdString());
}

// Where the move handle is in canvas pixels: the middle of the front edge of the bottom step.
std::optional<QPointF> StairEditor::moveHandleView() const
{
    const plynth::Stair* stair = selected();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (stair == nullptr || !ppm.has_value())
        return std::nullopt;
    return host_.canvas().sceneToView(QPointF(stair->x * *ppm, stair->y * *ppm));
}

// Where the turn handle is in canvas pixels: just beyond the point where the stair reaches the floor above.
std::optional<QPointF> StairEditor::rotateHandleView() const
{
    const plynth::Stair* stair = selected();
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (stair == nullptr || floor == nullptr || !ppm.has_value())
        return std::nullopt;
    const plynth::StairLayout layout = plynth::stairLayout(host_.document().plan(), *floor, *stair);
    if (!layout.status.ok())
        return std::nullopt;
    const QPointF from = host_.canvas().sceneToView(QPointF(stair->x * *ppm, stair->y * *ppm));
    const QPointF to = host_.canvas().sceneToView(QPointF(layout.arrival.x * *ppm, layout.arrival.y * *ppm));
    QPointF away = to - from;
    const double length = std::hypot(away.x(), away.y());
    if (length < 1.0)
        return std::nullopt;
    away /= length;
    return to + away * (Theme::instance().rotationHandle() * 2.0);
}

// Says whether the cursor is over the move or the turn handle.
bool StairEditor::overHandle(const QPoint& view) const
{
    const double reach = Theme::instance().rotationHandle() / 2.0 + 3.0;
    for (const std::optional<QPointF>& at : {moveHandleView(), rotateHandleView()}) {
        if (!at.has_value())
            continue;
        const QPointF d = QPointF(view) - *at;
        if (std::hypot(d.x(), d.y()) <= reach)
            return true;
    }
    return false;
}

// Starts turning or moving the selected stair when a handle is pressed.
bool StairEditor::pressHandle(const ToolMouse& mouse)
{
    const plynth::Stair* stair = selected();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (stair == nullptr || !ppm.has_value() || mouse.button != Qt::LeftButton)
        return false;
    const double reach = Theme::instance().rotationHandle() / 2.0 + 3.0;
    const std::optional<QPointF> turn = rotateHandleView();
    const std::optional<QPointF> move = moveHandleView();
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    if (turn.has_value() && std::hypot(mouse.view.x() - turn->x(), mouse.view.y() - turn->y()) <= reach) {
        mode_ = Mode::Rotate;
        stair_ = stair->id;
        key_ = host_.nextMergeKey();
        angleGrab_ = rotationToward(at.x - stair->x, at.y - stair->y) - stair->rotation;
        return true;
    }
    if (move.has_value() && std::hypot(mouse.view.x() - move->x(), mouse.view.y() - move->y()) <= reach) {
        mode_ = Mode::Move;
        stair_ = stair->id;
        key_ = host_.nextMergeKey();
        grab_ = QPointF(stair->x - at.x, stair->y - at.y);
        return true;
    }
    return false;
}

// Selects the stair under the click and prepares to move it once the mouse has travelled a few pixels.
bool StairEditor::pressBody(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return false;
    const QString id = host_.canvas().stairAt(mouse.scene);
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (id.isEmpty() || floor == nullptr || !ppm.has_value())
        return false;
    host_.selection().set(SelectionKind::Stair, id);
    const plynth::Stair* stair = plynth::findStair(*floor, id.toStdString());
    if (stair == nullptr || host_.document().isReadOnly())
        return true;
    stair_ = stair->id;
    key_ = host_.nextMergeKey();
    pressView_ = mouse.view;
    grab_ = QPointF(stair->x - mouse.scene.x() / *ppm, stair->y - mouse.scene.y() / *ppm);
    mode_ = Mode::MovePending;
    return true;
}

// Follows the cursor while a drag is in progress.
void StairEditor::moved(const ToolMouse& mouse)
{
    if (mode_ == Mode::None)
        return;
    if (mode_ == Mode::MovePending) {
        const QPoint d = mouse.view - pressView_;
        if (std::abs(d.x()) + std::abs(d.y()) < kDragStartPixels)
            return;
        mode_ = Mode::Move;
    }
    if (mode_ == Mode::Move)
        moveTo(mouse);
    else
        rotateTo(mouse);
}

// Runs MoveStairCommand for the cursor place, snapped to the grid unless Ctrl is held.
void StairEditor::moveTo(const ToolMouse& mouse)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    const plynth::Stair* stair = floor && ppm ? plynth::findStair(*floor, stair_) : nullptr;
    if (stair == nullptr)
        return;
    const plynth::Vec2 wanted = snapToGrid(
        host_, plynth::Vec2{mouse.scene.x() / *ppm + grab_.x(), mouse.scene.y() / *ppm + grab_.y()},
        mouse.modifiers & Qt::ControlModifier);
    if (wanted.x == stair->x && wanted.y == stair->y)
        return;
    host_.run(std::make_unique<plynth::MoveStairCommand>(stair_, wanted.x, wanted.y, stair->rotation, key_));
}

// Runs MoveStairCommand so the stair turns with the cursor, in 15 degree steps unless Alt is held.
void StairEditor::rotateTo(const ToolMouse& mouse)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    const plynth::Stair* stair = floor && ppm ? plynth::findStair(*floor, stair_) : nullptr;
    if (stair == nullptr)
        return;
    const double dx = mouse.scene.x() / *ppm - stair->x;
    const double dy = mouse.scene.y() / *ppm - stair->y;
    if (std::hypot(dx, dy) < 1e-6)
        return;
    double degrees = rotationToward(dx, dy) - angleGrab_;
    if (!(mouse.modifiers & Qt::AltModifier))
        degrees = std::round(degrees / kTurnStep) * kTurnStep;
    degrees = wrapRotation(degrees);
    if (degrees == stair->rotation)
        return;
    host_.run(std::make_unique<plynth::MoveStairCommand>(stair_, stair->x, stair->y, degrees, key_));
}

// Ends the drag. The merge key already made it one undo step.
void StairEditor::released()
{
    if (mode_ == Mode::None)
        return;
    mode_ = Mode::None;
    host_.canvas().viewport()->update();
}

// Forgets a drag.
void StairEditor::reset()
{
    mode_ = Mode::None;
}

// Draws the two handles of the selected stair: a square at the bottom step and a ring beyond the top.
void StairEditor::paintHandles(QPainter& painter, bool hover) const
{
    const Theme& t = Theme::instance();
    const bool active = hover || mode_ == Mode::Move;
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (const std::optional<QPointF> turn = rotateHandleView()) {
        const plynth::Stair* stair = selected();
        const plynth::Floor* floor = host_.floor();
        const std::optional<double> ppm = host_.pixelsPerMetre();
        if (stair != nullptr && floor != nullptr && ppm.has_value()) {
            const plynth::StairLayout layout = plynth::stairLayout(host_.document().plan(), *floor, *stair);
            painter.setPen(QPen(t.signalAmber(), 1.5));
            painter.drawLine(host_.canvas().sceneToView(QPointF(layout.arrival.x * *ppm, layout.arrival.y * *ppm)),
                             *turn);
        }
        painter.setPen(QPen(hover || mode_ == Mode::Rotate ? t.signalAmber() : t.muted(), 2));
        painter.setBrush(t.ground());
        painter.drawEllipse(*turn, t.rotationHandle() / 2.0, t.rotationHandle() / 2.0);
    }
    if (const std::optional<QPointF> move = moveHandleView()) {
        painter.setPen(QPen(active ? t.signalAmber() : t.signalBlue(), 2));
        painter.setBrush(t.ground());
        const double half = t.rotationHandle() / 2.0;
        painter.drawRect(QRectF(move->x() - half, move->y() - half, half * 2.0, half * 2.0));
    }
}

// The status bar hint for this tool.
QString StairTool::hint() const
{
    return QObject::tr("Click the bottom step and drag toward the top. Alt turns freely. Esc stops.");
}

// The tooltip of the tool strip button.
QString StairTool::tooltipText()
{
    return QObject::tr("Stair: click the bottom step and drag toward the top, then pick the type");
}

// Starts with nothing being dragged.
void StairTool::activate()
{
    cancel();
}

// Drops a drag in progress.
void StairTool::deactivate()
{
    cancel();
}

// A drag belongs to one floor.
void StairTool::floorChanged()
{
    cancel();
}

// A change made from outside (undo, redo) ends the drag.
void StairTool::documentChanged(unsigned changes)
{
    Q_UNUSED(changes);
    if (!menuOpen_ && dragging_)
        cancel();
}

// Forgets the drag and hides the preview.
void StairTool::cancel()
{
    dragging_ = false;
    end_.reset();
    previewType_ = "straight";
    host_.canvas().viewport()->update();
}

// The start of a drag: the cursor on the grid unless Ctrl is held.
plynth::Vec2 StairTool::snapStart(const ToolMouse& mouse) const
{
    const std::optional<double> ppm = host_.pixelsPerMetre();
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    return snapToGrid(host_, at, mouse.modifiers & Qt::ControlModifier);
}

// Remembers where the bottom step goes.
void StairTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button == Qt::RightButton) {
        cancel();
        return;
    }
    if (mouse.button != Qt::LeftButton || host_.floor() == nullptr || !host_.pixelsPerMetre().has_value())
        return;
    start_ = snapStart(mouse);
    end_.reset();
    free_ = mouse.modifiers & Qt::AltModifier;
    dragging_ = true;
}

// Follows the cursor with the preview.
void StairTool::mouseMoved(const ToolMouse& mouse)
{
    if (!dragging_ || !(mouse.buttons & Qt::LeftButton))
        return;
    updateEnd(mouse);
}

// Sets the place the arrow points at and repaints.
void StairTool::updateEnd(const ToolMouse& mouse)
{
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value())
        return;
    end_ = plynth::Vec2{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    free_ = mouse.modifiers & Qt::AltModifier;
    host_.canvas().viewport()->update();
}

// Alt turns the 15 degree steps off, so the preview is worked out again.
void StairTool::modifiersChanged()
{
    if (dragging_)
        host_.canvas().viewport()->update();
}

// A drag that went far enough opens the type menu at the cursor.
void StairTool::mouseReleased(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton || !dragging_)
        return;
    updateEnd(mouse);
    if (!end_.has_value() || plynth::distance(start_, *end_) < kMinDragMetres) {
        cancel();
        return;
    }
    chooseType(mouse.global);
}

// The stair the drag describes, with a type, or nothing while there is no drag.
std::optional<plynth::Stair> StairTool::stairNow(const std::string& type) const
{
    if (!dragging_ || !end_.has_value() || plynth::distance(start_, *end_) < 0.1)
        return std::nullopt;
    plynth::Stair stair;
    stair.type = type;
    stair.x = start_.x;
    stair.y = start_.y;
    double degrees = rotationToward(end_->x - start_.x, end_->y - start_.y);
    if (!free_)
        degrees = std::round(degrees / kTurnStep) * kTurnStep;
    stair.rotation = wrapRotation(degrees);
    return stair;
}

// Lists the stair types in a small menu. Moving over one previews it, picking one adds the stair.
void StairTool::chooseType(const QPoint& global)
{
    QMenu menu(host_.dialogParent());
    menu.setToolTipsVisible(true);
    for (const plynth::StairType* type : plynth::allStairTypes()) {
        const std::string id(type->id());
        const std::string_view name = type->name();
        QAction* action = menu.addAction(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));
        action->setToolTip(QObject::tr("Place a stair of this type"));
        QObject::connect(action, &QAction::hovered, &menu, [this, id]() {
            previewType_ = id;
            host_.canvas().viewport()->update();
        });
        QObject::connect(action, &QAction::triggered, &menu, [this, id]() { placeAs(id); });
    }
    menuOpen_ = true;
    menu.exec(global);
    menuOpen_ = false;
    cancel();
}

// Adds the stair as a command and selects it.
bool StairTool::placeAs(const std::string& type)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<plynth::Stair> stair = stairNow(type);
    if (floor == nullptr || !stair.has_value())
        return false;
    auto command = std::make_unique<plynth::AddStairCommand>(floor->id, *stair);
    const plynth::AddStairCommand* raw = command.get();
    if (!host_.run(std::move(command)).ok())
        return false;
    host_.selection().set(SelectionKind::Stair, QString::fromStdString(raw->newStairId()));
    return true;
}

// Esc drops the drag.
bool StairTool::keyPressed(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        cancel();
        return true;
    }
    return false;
}

// Shows the preview of a drag, for checks.
void StairTool::previewDrag(plynth::Vec2 from, plynth::Vec2 to)
{
    start_ = from;
    end_ = to;
    dragging_ = true;
    free_ = false;
    host_.canvas().viewport()->update();
}

// Draws the stair the drag describes, from core's preview layout.
void StairTool::paint(QPainter& painter)
{
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    const std::optional<plynth::Stair> stair = stairNow(previewType_);
    if (floor == nullptr || !ppm.has_value() || !stair.has_value())
        return;
    const plynth::StairLayout layout = plynth::previewStairLayout(host_.document().plan(), *floor, *stair);
    if (!layout.status.ok())
        return;
    paintStair(painter, makeOverlayStair(layout, *ppm), host_.canvas().viewportTransform(), StairLook::Preview);
}

}
