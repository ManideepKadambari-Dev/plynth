#include "ScaleTool.h"

#include "canvas/PlanCanvas.h"
#include "dialogs/ScaleDialog.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/Geometry.h>
#include <plynth/core/commands/FloorCommands.h>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>

#include <cmath>

namespace plynth::app {

// The status bar hint, which follows the step the tool is on.
QString ScaleTool::hint() const
{
    if (first_.has_value())
        return QObject::tr("Click the second point. Shift keeps it straight. Esc cancels.");
    return QObject::tr("Click the first point of a length you know on the plan image.");
}

// Drops a half made measurement.
void ScaleTool::deactivate()
{
    first_.reset();
    last_.reset();
}

// Places the first point, then the second, and then asks for the real distance.
void ScaleTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button == Qt::RightButton) {
        first_.reset();
        host_.canvas().viewport()->update();
        host_.hintChanged();
        return;
    }
    if (mouse.button != Qt::LeftButton)
        return;
    last_ = mouse;
    if (!first_.has_value()) {
        first_ = mouse.scene;
        host_.hintChanged();
        host_.canvas().viewport()->update();
        return;
    }
    finish(endFor(mouse));
}

// Remembers the cursor so the line follows it.
void ScaleTool::mouseMoved(const ToolMouse& mouse)
{
    last_ = mouse;
    host_.canvas().viewport()->update();
}

// Hides the line when the cursor leaves the plan.
void ScaleTool::mouseLeft()
{
    last_.reset();
    host_.canvas().viewport()->update();
}

// Esc cancels the measurement.
bool ScaleTool::keyPressed(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && first_.has_value()) {
        first_.reset();
        host_.hintChanged();
        host_.canvas().viewport()->update();
        return true;
    }
    return false;
}

// Redraws the line when Shift goes down or up.
void ScaleTool::modifiersChanged()
{
    host_.canvas().viewport()->update();
}

// The second point for the cursor, kept horizontal or vertical while Shift is held.
QPointF ScaleTool::endFor(const ToolMouse& mouse) const
{
    QPointF end = mouse.scene;
    if (first_.has_value() && (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        const QPointF d = end - *first_;
        if (std::abs(d.x()) >= std::abs(d.y()))
            end.setY(first_->y());
        else
            end.setX(first_->x());
    }
    return end;
}

// Asks for the real distance and runs SetScaleCommand with the result.
void ScaleTool::finish(QPointF second)
{
    const QPointF a = *first_;
    first_.reset();
    host_.hintChanged();
    host_.canvas().viewport()->update();

    const double pixels = std::hypot(second.x() - a.x(), second.y() - a.y());
    if (pixels < plynth::kMinScalePixelDistance) {
        host_.showMessage(QObject::tr("The two points are too close together. Pick points further apart."));
        return;
    }
    const std::optional<double> current = host_.pixelsPerMetre();
    const plynth::Floor* floor = host_.floor();
    const bool hasMarks = floor != nullptr && !floor->points.empty();
    std::optional<double> preset;
    if (current.has_value())
        preset = pixels / *current;
    ScaleDialog dialog(pixels, preset, current.has_value() && hasMarks, host_.dialogParent());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const plynth::ScaleMeasure measure =
        plynth::scaleFromImagePoints({a.x(), a.y()}, {second.x(), second.y()}, dialog.metres());
    if (!measure.status.ok()) {
        host_.showMessage(QString::fromStdString(measure.status.message));
        return;
    }
    const plynth::ScaleKeep keep = dialog.keepMarksOnImage() ? plynth::ScaleKeep::ImagePositions
                                                             : plynth::ScaleKeep::Metres;
    if (host_.run(std::make_unique<plynth::SetScaleCommand>(measure.pixelsPerMetre, keep)).ok())
        host_.requestTool(ToolId::Select);
}

// Draws the amber line from the first point with its pixel length in mono type.
void ScaleTool::paint(QPainter& painter)
{
    if (!first_.has_value())
        return;
    const Theme& t = Theme::instance();
    PlanCanvas& canvas = host_.canvas();
    const QPointF a = canvas.sceneToView(*first_);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.signalAmber());
    painter.drawEllipse(a, t.pointDot() / 2.0, t.pointDot() / 2.0);
    if (!last_.has_value())
        return;
    const QPointF endScene = endFor(*last_);
    const QPointF b = canvas.sceneToView(endScene);
    painter.setPen(QPen(t.signalAmber(), 2));
    painter.drawLine(a, b);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(b, t.pointDot() / 2.0, t.pointDot() / 2.0);

    const double pixels = std::hypot(endScene.x() - first_->x(), endScene.y() - first_->y());
    const QString text = QObject::tr("%1 px").arg(pixels, 0, 'f', 1);
    painter.setFont(t.monoFont(t.sizeNormal()));
    const QFontMetrics fm(painter.font());
    const QPointF mid = (a + b) / 2.0;
    QRectF box(0, 0, fm.horizontalAdvance(text) + t.unit() * 3, fm.height() + t.unit());
    box.moveCenter(mid + QPointF(0, -t.unit() * 5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.panel());
    painter.drawRoundedRect(box, t.radius(), t.radius());
    painter.setPen(t.signalAmber());
    painter.drawText(box, Qt::AlignCenter, text);
}

}
