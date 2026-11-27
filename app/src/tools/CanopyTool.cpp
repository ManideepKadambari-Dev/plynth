#include "CanopyTool.h"

#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/SnapGuides.h"
#include "tools/ToolController.h"

#include <plynth/core/Canopies.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/commands/CanopyCommands.h>

#include <QAction>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPolygonF>

#include <cmath>
#include <memory>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// How far from a handle, in canvas pixels, a press still takes it.
double handleReach()
{
    return Theme::instance().handleSize() / 2.0 + 3.0;
}

// Snaps a place like a wall end: to points, walls, the grid and lined up with other places. Ctrl places freely,
// Shift keeps it level or upright from the place before.
plynth::SnapResult snapCorner(const ToolController& host, plynth::Vec2 at, Qt::KeyboardModifiers modifiers,
                              const std::vector<plynth::Vec2>& alignTo, std::optional<plynth::Vec2> from)
{
    plynth::SnapResult free;
    free.position = at;
    const plynth::Floor* floor = host.floor();
    if (floor == nullptr || (modifiers & Qt::ControlModifier))
        return free;
    plynth::SnapOptions options;
    options.radius = host.snapRadiusMetres();
    if (host.snapToGrid())
        options.gridSize = host.document().project().settings.gridSize;
    options.alignTo = alignTo;
    if (from.has_value() && (modifiers & Qt::ShiftModifier))
        options.orthogonalFrom = *from;
    return plynth::snapPosition(*floor, at, options);
}

}

// The selected canopy when it is on the shown floor and the project can be edited.
const plynth::Canopy* CanopyEditor::selected() const
{
    const plynth::Floor* floor = host_.floor();
    if (floor == nullptr || host_.document().isReadOnly() || host_.selection().kind() != SelectionKind::Canopy)
        return nullptr;
    return plynth::findCanopy(*floor, host_.selection().id().toStdString());
}

// The corner of the selected canopy whose handle is under a canvas position, or -1.
int CanopyEditor::handleAt(const QPoint& view) const
{
    const plynth::Canopy* canopy = selected();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (canopy == nullptr || !ppm.has_value())
        return -1;
    for (int i = static_cast<int>(canopy->outline.size()) - 1; i >= 0; --i) {
        const plynth::PlanXY& p = canopy->outline[static_cast<std::size_t>(i)];
        const QPointF at = host_.canvas().sceneToView(QPointF(p.x * *ppm, p.y * *ppm));
        if (std::hypot(view.x() - at.x(), view.y() - at.y()) <= handleReach())
            return i;
    }
    return -1;
}

// Says whether the cursor is over a corner handle.
bool CanopyEditor::overHandle(const QPoint& view) const
{
    return handleAt(view) >= 0;
}

// Starts dragging a corner of the selected canopy when its handle is pressed.
bool CanopyEditor::pressHandle(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return false;
    const int corner = handleAt(mouse.view);
    if (corner < 0)
        return false;
    canopy_ = selected()->id;
    corner_ = corner;
    key_ = host_.nextMergeKey();
    snap_.reset();
    return true;
}

// Selects the canopy under the press.
bool CanopyEditor::pressBody(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return false;
    const QString id = host_.canvas().canopyAt(mouse.scene);
    if (id.isEmpty())
        return false;
    host_.selection().set(SelectionKind::Canopy, id);
    return true;
}

// Moves the dragged corner to the snapped cursor place with SetCanopyCommand. A place that would make the
// outline cross itself is skipped, so the drag carries on from the last good one.
void CanopyEditor::moved(const ToolMouse& mouse)
{
    if (corner_ < 0 || !(mouse.buttons & Qt::LeftButton))
        return;
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    const plynth::Canopy* canopy = floor != nullptr ? plynth::findCanopy(*floor, canopy_) : nullptr;
    if (canopy == nullptr || !ppm.has_value() || static_cast<std::size_t>(corner_) >= canopy->outline.size()) {
        reset();
        return;
    }
    std::vector<plynth::Vec2> others;
    for (std::size_t i = 0; i < canopy->outline.size(); ++i)
        if (static_cast<int>(i) != corner_)
            others.push_back(plynth::Vec2{canopy->outline[i].x, canopy->outline[i].y});
    const plynth::SnapResult snap =
        snapCorner(host_, plynth::Vec2{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm}, mouse.modifiers, others, std::nullopt);
    snap_ = snap;
    host_.canvas().viewport()->update();
    plynth::Canopy values = *canopy;
    plynth::PlanXY& corner = values.outline[static_cast<std::size_t>(corner_)];
    if (corner.x == snap.position.x && corner.y == snap.position.y)
        return;
    corner.x = snap.position.x;
    corner.y = snap.position.y;
    if (!plynth::checkCanopy(values).ok())
        return;
    host_.run(std::make_unique<plynth::SetCanopyCommand>(canopy_, values, key_));
}

// Ends the drag. The merge key already made it one undo step.
void CanopyEditor::released()
{
    if (corner_ < 0)
        return;
    reset();
    host_.canvas().viewport()->update();
}

// Forgets a drag.
void CanopyEditor::reset()
{
    corner_ = -1;
    canopy_.clear();
    snap_.reset();
}

// Draws a square handle on every corner of the selected canopy, amber for the one dragged or hovered, and the
// snap guides of a drag.
void CanopyEditor::paintHandles(QPainter& painter, bool hover) const
{
    const plynth::Canopy* canopy = selected();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (canopy == nullptr || !ppm.has_value())
        return;
    const Theme& t = Theme::instance();
    if (corner_ >= 0 && snap_.has_value())
        paintSnap(painter, host_.canvas(), *ppm, *snap_);
    const QPoint cursor = host_.canvas().viewport()->mapFromGlobal(QCursor::pos());
    const int hovered = hover ? handleAt(cursor) : -1;
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double half = t.handleSize() / 2.0;
    for (std::size_t i = 0; i < canopy->outline.size(); ++i) {
        const plynth::PlanXY& p = canopy->outline[i];
        const QPointF at = host_.canvas().sceneToView(QPointF(p.x * *ppm, p.y * *ppm));
        const bool active = static_cast<int>(i) == corner_ || static_cast<int>(i) == hovered;
        painter.setPen(QPen(active ? t.signalAmber() : t.signalBlue(), 2));
        painter.setBrush(t.ground());
        painter.drawRect(QRectF(at.x() - half, at.y() - half, half * 2.0, half * 2.0));
    }
}

// The status bar hint for this tool.
QString CanopyTool::hint() const
{
    return QObject::tr("Click the corners of the canopy, then click the first corner or press Enter and pick the roof type. "
                       "Backspace takes back a corner. Shift keeps it straight, Ctrl places freely. Esc stops.");
}

// Starts with no outline.
void CanopyTool::activate()
{
    cancel();
}

// Drops the outline and a drag in progress.
void CanopyTool::deactivate()
{
    editor_.reset();
    cancel();
    lastScene_.reset();
}

// An outline belongs to one floor.
void CanopyTool::floorChanged()
{
    editor_.reset();
    cancel();
}

// The outline is only places, so a change made elsewhere keeps it. The preview follows the new plan.
void CanopyTool::documentChanged(unsigned changes)
{
    Q_UNUSED(changes);
    if (!menuOpen_)
        refreshPreview();
}

// Forgets the outline being drawn.
void CanopyTool::cancel()
{
    corners_.clear();
    refreshPreview();
}

// Snaps the cursor like a corner after the ones already clicked.
plynth::SnapResult CanopyTool::snapFor(QPointF scene, Qt::KeyboardModifiers modifiers) const
{
    const double ppm = host_.pixelsPerMetre().value_or(1.0);
    const std::optional<plynth::Vec2> from =
        corners_.empty() ? std::nullopt : std::optional<plynth::Vec2>(corners_.back());
    return snapCorner(host_, plynth::Vec2{scene.x() / ppm, scene.y() / ppm}, modifiers, corners_, from);
}

// Says whether a canvas position is on the first corner of an outline that can be closed.
bool CanopyTool::nearFirstCorner(const QPoint& view) const
{
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (corners_.size() < 3 || !ppm.has_value())
        return false;
    const QPointF first = host_.canvas().sceneToView(QPointF(corners_.front().x * *ppm, corners_.front().y * *ppm));
    return std::hypot(view.x() - first.x(), view.y() - first.y()) <= handleReach() + 2.0;
}

// Adds a corner, closes the outline on its first corner, or selects and edits a canopy when no outline is
// being drawn. Right click stops.
void CanopyTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button == Qt::RightButton) {
        cancel();
        return;
    }
    if (mouse.button != Qt::LeftButton || host_.floor() == nullptr || !host_.pixelsPerMetre().has_value())
        return;
    if (corners_.empty()) {
        if (editor_.pressHandle(mouse) || editor_.pressBody(mouse))
            return;
        host_.selection().clear();
    }
    if (nearFirstCorner(mouse.view)) {
        finish(mouse.global);
        return;
    }
    const plynth::Vec2 at = snapFor(mouse.scene, mouse.modifiers).position;
    if (!corners_.empty() && plynth::distance(corners_.back(), at) < 1e-6)
        return;
    corners_.push_back(at);
    refreshPreview();
}

// Moves the preview with the cursor, or the dragged corner.
void CanopyTool::mouseMoved(const ToolMouse& mouse)
{
    if (editor_.busy()) {
        editor_.moved(mouse);
        return;
    }
    lastScene_ = mouse.scene;
    refreshPreview();
}

// Ends a corner drag.
void CanopyTool::mouseReleased(const ToolMouse& mouse)
{
    if (mouse.button == Qt::LeftButton)
        editor_.released();
}

// Hides the preview when the cursor leaves the plan.
void CanopyTool::mouseLeft()
{
    lastScene_.reset();
    refreshPreview();
}

// Enter closes the outline, Backspace takes back the last corner, Esc stops or clears the selection.
bool CanopyTool::keyPressed(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (corners_.size() < 3)
            return false;
        finish(QCursor::pos());
        return true;
    }
    if (event->key() == Qt::Key_Backspace && !corners_.empty()) {
        corners_.pop_back();
        refreshPreview();
        return true;
    }
    if (event->key() == Qt::Key_Escape) {
        if (corners_.empty())
            host_.selection().clear();
        cancel();
        return true;
    }
    return false;
}

// Shift and Control change the snapped place, so the preview is worked out again.
void CanopyTool::modifiersChanged()
{
    refreshPreview();
}

// Works out the snapped place under the last cursor position and repaints.
void CanopyTool::refreshPreview()
{
    preview_.reset();
    if (lastScene_.has_value() && host_.pixelsPerMetre().has_value())
        preview_ = snapFor(*lastScene_, QGuiApplication::keyboardModifiers());
    host_.canvas().viewport()->update();
}

// Checks the outline with core, then lists the roof types in a small menu at a screen place. Picking one adds
// the canopy. Closing the menu keeps the outline, so the user can carry on.
void CanopyTool::finish(const QPoint& global)
{
    if (corners_.size() < 3)
        return;
    plynth::Canopy trial;
    for (const plynth::Vec2& corner : corners_)
        trial.outline.push_back(plynth::PlanXY{corner.x, corner.y});
    const plynth::Status check = plynth::checkCanopy(trial);
    if (!check.ok()) {
        host_.showMessage(fromCore(check.message));
        return;
    }
    QMenu menu(host_.dialogParent());
    menu.setToolTipsVisible(true);
    menu.addSection(QObject::tr("Roof type"));
    std::string picked;
    for (const plynth::RoofType* type : plynth::allRoofTypes()) {
        const std::string id(type->id());
        QAction* action = menu.addAction(fromCore(type->name()));
        action->setToolTip(QObject::tr("Put a %1 roof on this canopy").arg(fromCore(type->name()).toLower()));
        QObject::connect(action, &QAction::triggered, &menu, [&picked, id]() { picked = id; });
    }
    menuOpen_ = true;
    menu.exec(global);
    menuOpen_ = false;
    if (picked.empty()) {
        refreshPreview();
        return;
    }
    const std::vector<plynth::Vec2> outline = corners_;
    if (placeOutline(outline, picked))
        cancel();
}

// Runs AddCanopyCommand on the shown floor with the canopy's start values and the roof type, then selects it.
bool CanopyTool::placeOutline(const std::vector<plynth::Vec2>& outline, const std::string& roofType)
{
    const plynth::Floor* floor = host_.floor();
    if (floor == nullptr || host_.document().isReadOnly())
        return false;
    plynth::Canopy canopy;
    for (const plynth::Vec2& corner : outline)
        canopy.outline.push_back(plynth::PlanXY{corner.x, corner.y});
    canopy.roof.type = roofType;
    auto command = std::make_unique<plynth::AddCanopyCommand>(floor->id, canopy);
    const plynth::AddCanopyCommand* raw = command.get();
    if (!host_.run(std::move(command)).ok())
        return false;
    host_.selection().set(SelectionKind::Canopy, fromCore(raw->newCanopyId()));
    return true;
}

// Shows an outline through places as if they had been clicked, the last one standing for the cursor.
void CanopyTool::previewOutline(const std::vector<plynth::Vec2>& corners)
{
    corners_ = corners;
    preview_.reset();
    lastScene_.reset();
    if (!corners_.empty()) {
        plynth::SnapResult cursor;
        cursor.position = corners_.back();
        corners_.pop_back();
        preview_ = cursor;
    }
    host_.canvas().viewport()->update();
}

// Draws the outline being drawn in amber with a light fill, its corners, a ring on the first corner once it can be
// closed, and the snap guides. Without an outline it draws the handles of the selected canopy.
void CanopyTool::paint(QPainter& painter)
{
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!ppm.has_value())
        return;
    const Theme& t = Theme::instance();
    PlanCanvas& canvas = host_.canvas();
    if (corners_.empty()) {
        editor_.paintHandles(painter, true);
        if (preview_.has_value() && !editor_.busy())
            paintSnap(painter, canvas, *ppm, *preview_);
        return;
    }
    QPolygonF ring;
    for (const plynth::Vec2& corner : corners_)
        ring << canvas.sceneToView(QPointF(corner.x * *ppm, corner.y * *ppm));
    if (preview_.has_value())
        ring << canvas.sceneToView(QPointF(preview_->position.x * *ppm, preview_->position.y * *ppm));
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (ring.size() >= 3) {
        QColor fill = t.signalAmber();
        fill.setAlpha(t.roomFillAlpha());
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPolygon(ring);
    }
    painter.setPen(QPen(t.signalAmber(), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(ring);
    if (ring.size() >= 3) {
        QPen closing(t.signalAmber(), 1.0, Qt::CustomDashLine);
        closing.setDashPattern({static_cast<qreal>(t.proposalDash()), static_cast<qreal>(t.proposalDash())});
        painter.setPen(closing);
        painter.drawLine(ring.last(), ring.first());
    }
    const double dot = t.pointDot() / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.signalAmber());
    for (std::size_t i = 0; i < corners_.size(); ++i)
        painter.drawEllipse(ring[static_cast<qsizetype>(i)], dot, dot);
    if (corners_.size() >= 3) {
        painter.setPen(QPen(t.signalAmber(), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ring.first(), dot + 4, dot + 4);
    }
    if (preview_.has_value())
        paintSnap(painter, canvas, *ppm, *preview_);
}

}
