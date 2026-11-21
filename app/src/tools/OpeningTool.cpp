#include "OpeningTool.h"

#include "canvas/OpeningPaint.h"
#include "canvas/OverlayBuilder.h"
#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/Openings.h>
#include <plynth/core/commands/OpeningCommands.h>

#include <QGuiApplication>
#include <QPainter>

namespace plynth::app {

namespace {

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// The status bar hint for this tool.
QString OpeningTool::hint() const
{
    return QObject::tr("Click a wall to add an opening. Ctrl places freely. The wall view sets the height.");
}

// Starts with no preview.
void OpeningTool::activate()
{
    preview_.reset();
    lastMessage_.clear();
}

// Hides the preview.
void OpeningTool::deactivate()
{
    preview_.reset();
    lastScene_.reset();
    lastMessage_.clear();
}

// A left click adds the previewed opening and selects it.
void OpeningTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    lastScene_ = mouse.scene;
    refreshPreview();
    if (!preview_.has_value())
        return;
    auto command = std::make_unique<plynth::AddOpeningCommand>(preview_->opening);
    const plynth::AddOpeningCommand* raw = command.get();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (!host_.run(std::move(command)).ok())
        return;
    if (ppm.has_value())
        host_.selection().setClickAt(mouse.scene.x() / *ppm, mouse.scene.y() / *ppm);
    host_.selection().set(SelectionKind::Opening, QString::fromStdString(raw->openingId()));
}

// Moves the preview with the cursor.
void OpeningTool::mouseMoved(const ToolMouse& mouse)
{
    lastScene_ = mouse.scene;
    refreshPreview();
}

// Hides the preview when the cursor leaves the plan.
void OpeningTool::mouseLeft()
{
    lastScene_.reset();
    refreshPreview();
}

// Ctrl changes the snapping, so the preview is worked out again.
void OpeningTool::modifiersChanged()
{
    refreshPreview();
}

// A changed plan or changed defaults can move the preview.
void OpeningTool::documentChanged(unsigned changes)
{
    Q_UNUSED(changes);
    refreshPreview();
}

// Finds the wall and place under the cursor with core's placeOpeningAt and checks that the opening fits.
void OpeningTool::refreshPreview()
{
    preview_.reset();
    QString message;
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (lastScene_.has_value() && floor != nullptr && ppm.has_value()) {
        const plynth::Opening base = host_.newOpening(plynth::Id(), 0.0);
        const bool free = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
        const plynth::Vec2 at{lastScene_->x() / *ppm, lastScene_->y() / *ppm};
        const plynth::OpeningSpot spot = plynth::placeOpeningAt(*floor, at, host_.snapRadiusMetres(), base.width,
                                                                free ? 0.0 : plynth::kOpeningSnapStep);
        if (spot.status.ok()) {
            Preview preview;
            preview.opening = host_.newOpening(spot.wall, spot.offset);
            preview.fit = plynth::checkOpeningFit(*floor, preview.opening);
            preview.shape = makeOverlayOpening(*floor, preview.opening, *ppm);
            if (!preview.fit.ok())
                message = fromCore(preview.fit.message);
            if (preview.shape.valid)
                preview_ = std::move(preview);
        } else if (spot.status.code == plynth::ErrorCode::InvalidValue) {
            message = fromCore(spot.status.message);
        }
    }
    if (!message.isEmpty() && message != lastMessage_)
        host_.showMessage(message);
    lastMessage_ = message;
    host_.canvas().viewport()->update();
}

// Draws the preview in amber: solid when the opening fits, dashed when it does not.
void OpeningTool::paint(QPainter& painter)
{
    if (!preview_.has_value())
        return;
    const Theme& t = Theme::instance();
    QColor tint = t.signalAmber();
    tint.setAlpha(t.overlayAlpha());
    OpeningPaintStyle style;
    style.line = t.signalAmber();
    style.gap = false;
    if (!preview_->fit.ok())
        tint.setAlpha(0);
    style.tint = tint;
    style.dashedFootprint = !preview_->fit.ok();
    style.outlineColor = t.signalAmber();
    paintOpening(painter, preview_->shape, host_.canvas().viewportTransform(), style);
}

}
