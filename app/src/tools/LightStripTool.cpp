#include "LightStripTool.h"

#include "canvas/LightPaint.h"
#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "tools/ToolController.h"

#include <plynth/core/commands/LightStripCommands.h>

#include <QAction>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QObject>
#include <QPainter>

#include <cmath>
#include <memory>

namespace plynth::app {

namespace {

constexpr int kDragStartPixels = 3;
constexpr double kFinishPixels = 6.0;

}

// The plain name of a light strip kind.
QString lightStripKindText(plynth::LightStripKind kind)
{
    switch (kind) {
    case plynth::LightStripKind::Cove: return QObject::tr("Cove strip");
    case plynth::LightStripKind::Soffit: return QObject::tr("Soffit strip");
    case plynth::LightStripKind::Edge: return QObject::tr("Edge strip");
    case plynth::LightStripKind::Step: return QObject::tr("Step lights");
    }
    return QString();
}

// Where a strip of a kind goes and which way it shines.
QString lightStripKindTip(plynth::LightStripKind kind)
{
    switch (kind) {
    case plynth::LightStripKind::Cove:
        return QObject::tr("Along a ceiling edge, hanging just below its height and shining up onto the ceiling");
    case plynth::LightStripKind::Soffit:
        return QObject::tr("Under a cabinet, a shelf or an overhang, sitting just above its height and shining down");
    case plynth::LightStripKind::Edge:
        return QObject::tr("Along a wall or a slab edge, centred on its height and shining to the right of the line as drawn");
    case plynth::LightStripKind::Step:
        return QObject::tr("Under the nose of every step of a stair, shining down and forward. It follows the stair");
    }
    return QString();
}

// Selects the strip under the press, preferring a point handle of the selected strip, and prepares a drag.
bool LightStripEditor::pressBody(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return false;
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value())
        return false;
    QString id;
    int point = -1;
    if (host_.selection().kind() == SelectionKind::LightStrip) {
        point = host_.canvas().lightStripPointAt(host_.selection().id(), mouse.scene);
        if (point >= 0)
            id = host_.selection().id();
    }
    if (id.isEmpty())
        id = host_.canvas().lightStripAt(mouse.scene);
    if (id.isEmpty())
        return false;
    host_.selection().set(SelectionKind::LightStrip, id);
    const plynth::LightStrip* strip = plynth::findLightStrip(*floor, id.toStdString());
    if (strip == nullptr || host_.document().isReadOnly() || strip->kind == plynth::LightStripKind::Step ||
        strip->points.empty())
        return true;
    strip_ = strip->id;
    point_ = point;
    startPoints_ = strip->points;
    key_ = host_.nextMergeKey();
    pressView_ = mouse.view;
    pressMetres_ = QPointF(mouse.scene.x() / *ppm, mouse.scene.y() / *ppm);
    mode_ = Mode::MovePending;
    return true;
}

// Moves the pressed point, or the whole line, once the mouse has travelled a few pixels. Points snap to the grid
// unless Ctrl is held; a whole line moves so its first point lands on the grid.
void LightStripEditor::moved(const ToolMouse& mouse)
{
    if (mode_ == Mode::None || !(mouse.buttons & Qt::LeftButton))
        return;
    if (mode_ == Mode::MovePending) {
        const QPoint d = mouse.view - pressView_;
        if (std::abs(d.x()) + std::abs(d.y()) < kDragStartPixels)
            return;
        mode_ = Mode::Move;
    }
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    const plynth::LightStrip* strip = floor ? plynth::findLightStrip(*floor, strip_) : nullptr;
    if (strip == nullptr || !ppm.has_value() || startPoints_.empty()) {
        mode_ = Mode::None;
        return;
    }
    const bool free = (mouse.modifiers & Qt::ControlModifier) != 0;
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};
    plynth::LightStrip values = *strip;
    values.points = startPoints_;
    if (point_ >= 0 && point_ < static_cast<int>(values.points.size())) {
        const plynth::Vec2 snapped = host_.snapLight(at, free);
        values.points[static_cast<std::size_t>(point_)] = plynth::PlanXY{snapped.x, snapped.y};
    } else {
        const plynth::PlanXY first = startPoints_.front();
        const plynth::Vec2 snapped = host_.snapLight(
            plynth::Vec2{first.x + at.x - pressMetres_.x(), first.y + at.y - pressMetres_.y()}, free);
        const double dx = snapped.x - first.x;
        const double dy = snapped.y - first.y;
        for (plynth::PlanXY& p : values.points) {
            p.x += dx;
            p.y += dy;
        }
    }
    if (values == *strip)
        return;
    if (host_.run(std::make_unique<plynth::SetLightStripCommand>(strip_, values, key_)).ok())
        return;
    mode_ = Mode::None;
}

// Ends the drag. The merge key already made it one undo step.
void LightStripEditor::released()
{
    mode_ = Mode::None;
}

// Forgets a drag in progress.
void LightStripEditor::reset()
{
    mode_ = Mode::None;
    strip_.clear();
    point_ = -1;
    startPoints_.clear();
}

// The status bar hint for this tool, which changes once a line is started.
QString LightStripTool::hint() const
{
    if (!draft_.empty())
        return QObject::tr("Click the next point. Enter, a right click or a click on the last point ends the strip. "
                           "Ctrl places freely. Esc stops.");
    return QObject::tr("Click along a ceiling edge, cabinet, shelf or wall to draw a light strip, or click a stair for "
                       "step lights. Drag a strip or its points to move them, Delete removes it.");
}

// Drops the line and any drag when another tool takes over.
void LightStripTool::deactivate()
{
    cancel();
    editor_.reset();
}

// Starts or extends a line, ends it on a click at its last point, or adds step lights to a clicked stair. With
// no line started, a press on a strip selects it instead.
void LightStripTool::mousePressed(const ToolMouse& mouse)
{
    if (menuOpen_)
        return;
    if (mouse.button == Qt::RightButton) {
        if (!draft_.empty())
            finish(mouse.global);
        return;
    }
    if (mouse.button != Qt::LeftButton)
        return;
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value() || host_.document().isReadOnly())
        return;
    const bool free = (mouse.modifiers & Qt::ControlModifier) != 0;
    if (draft_.empty()) {
        if (editor_.pressBody(mouse))
            return;
        const QString stair = host_.canvas().stairAt(mouse.scene);
        if (!stair.isEmpty() && !free) {
            host_.placeStepStrip(stair.toStdString());
            return;
        }
    } else {
        const QPointF last = host_.canvas().sceneToView(QPointF(draft_.back().x * *ppm, draft_.back().y * *ppm));
        if (std::hypot(last.x() - mouse.view.x(), last.y() - mouse.view.y()) <= kFinishPixels) {
            finish(mouse.global);
            return;
        }
    }
    const plynth::Vec2 at = host_.snapLight(plynth::Vec2{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm}, free);
    draft_.push_back(at);
    hover_ = at;
    if (draft_.size() == 1)
        host_.hintChanged();
    host_.canvas().viewport()->update();
}

// Moves the pressed strip, or follows the mouse with the next piece of the line.
void LightStripTool::mouseMoved(const ToolMouse& mouse)
{
    if (editor_.busy()) {
        editor_.moved(mouse);
        return;
    }
    if (draft_.empty())
        return;
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value())
        return;
    hover_ = host_.snapLight(plynth::Vec2{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm},
                             (mouse.modifiers & Qt::ControlModifier) != 0);
    host_.canvas().viewport()->update();
}

// Ends a drag.
void LightStripTool::mouseReleased(const ToolMouse& mouse)
{
    if (mouse.button == Qt::LeftButton)
        editor_.released();
}

// Hides the piece that follows the mouse while it is off the plan.
void LightStripTool::mouseLeft()
{
    hover_.reset();
    host_.canvas().viewport()->update();
}

// Enter ends the line. Esc drops the line, or clears the selection when no line is started.
bool LightStripTool::keyPressed(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !draft_.empty()) {
        finish(QCursor::pos());
        return true;
    }
    if (event->key() != Qt::Key_Escape)
        return false;
    if (!draft_.empty()) {
        cancel();
        return true;
    }
    editor_.reset();
    host_.selection().clear();
    return true;
}

// A line on the old floor is dropped when another floor is shown.
void LightStripTool::floorChanged()
{
    cancel();
    editor_.reset();
}

// Draws the line being drawn, with the piece that follows the mouse, in amber.
void LightStripTool::paint(QPainter& painter)
{
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (draft_.empty() || !ppm.has_value())
        return;
    QPolygonF points;
    for (const plynth::Vec2& p : draft_)
        points << host_.canvas().sceneToView(QPointF(p.x * *ppm, p.y * *ppm));
    QPolygonF line = points;
    if (hover_.has_value())
        line << host_.canvas().sceneToView(QPointF(hover_->x * *ppm, hover_->y * *ppm));
    paintLightStrip(painter, QVector<QPolygonF>{line}, points, QString(), true, false);
}

// For checks: puts the tool in the middle of drawing a line through these points.
void LightStripTool::showDraft(const std::vector<plynth::Vec2>& points)
{
    cancel();
    draft_ = points;
    if (!draft_.empty())
        hover_ = draft_.back();
    host_.hintChanged();
    host_.canvas().viewport()->update();
}

// Asks for the kind of the drawn line and adds it. Closing the menu without a choice drops the line.
void LightStripTool::finish(const QPoint& global)
{
    if (draft_.size() < 2) {
        cancel();
        return;
    }
    QMenu menu(host_.dialogParent());
    menu.setToolTipsVisible(true);
    for (const plynth::LightStripKind kind :
         {plynth::LightStripKind::Cove, plynth::LightStripKind::Soffit, plynth::LightStripKind::Edge}) {
        QAction* action = menu.addAction(lightStripKindText(kind));
        action->setToolTip(lightStripKindTip(kind));
        QObject::connect(action, &QAction::triggered, &menu, [this, kind]() { host_.placeLightStrip(kind, draft_); });
    }
    menuOpen_ = true;
    menu.exec(global);
    menuOpen_ = false;
    cancel();
}

// Drops the line being drawn.
void LightStripTool::cancel()
{
    const bool had = !draft_.empty();
    draft_.clear();
    hover_.reset();
    if (had) {
        host_.hintChanged();
        host_.canvas().viewport()->update();
    }
}

}
