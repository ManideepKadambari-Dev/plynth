#include "ToolController.h"

#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "detection/DetectionSession.h"
#include "theme/Theme.h"
#include "tools/DetectTool.h"
#include "tools/OpeningTool.h"
#include "tools/RoomTool.h"
#include "tools/ScaleTool.h"
#include "tools/SelectTool.h"
#include "tools/WallTool.h"

#include <plynth/core/Openings.h>

namespace plynth::app {

namespace {

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// Makes the tools and starts with the Select tool.
ToolController::ToolController(plynth::Document& document, PlanCanvas* canvas, Selection* selection,
                               QWidget* dialogParent, QObject* parent)
    : QObject(parent), document_(document), canvas_(canvas), selection_(selection), dialogParent_(dialogParent)
{
    tools_[static_cast<size_t>(ToolId::Select)] = std::make_unique<SelectTool>(*this);
    tools_[static_cast<size_t>(ToolId::Scale)] = std::make_unique<ScaleTool>(*this);
    tools_[static_cast<size_t>(ToolId::Wall)] = std::make_unique<WallTool>(*this);
    tools_[static_cast<size_t>(ToolId::Opening)] = std::make_unique<OpeningTool>(*this);
    tools_[static_cast<size_t>(ToolId::Room)] = std::make_unique<RoomTool>(*this);
    detection_ = new DetectionSession(*this, this);
    tools_[static_cast<size_t>(ToolId::Detect)] = std::make_unique<DetectTool>(*this);
    connect(detection_, &DetectionSession::changed, this, [this]() {
        if (canvas_ != nullptr)
            canvas_->setProposals(detection_->overlay());
    });
    canvas_->setTool(tool(active_));
    tool(active_)->activate();
}

// Stops detection and takes the tool away from the canvas, if the canvas is still there.
ToolController::~ToolController()
{
    detection_->blockSignals(true);
    detection_->setActive(false);
    if (canvas_ != nullptr)
        canvas_->setTool(nullptr);
}

// The tool object for an id, or null when that tool does not exist yet.
Tool* ToolController::tool(ToolId id) const
{
    return tools_[static_cast<size_t>(id)].get();
}

// Switches to another tool when it can be used now.
bool ToolController::setActive(ToolId id)
{
    if (id == active_)
        return true;
    if (!isAvailable(id))
        return false;
    tool(active_)->deactivate();
    active_ = id;
    canvas_->setTool(tool(active_));
    tool(active_)->activate();
    emit activeChanged(active_);
    emit gridChanged();
    hintChanged();
    return true;
}

// Select is always on. The others need an image, and the drawing tools need a scale as well.
bool ToolController::isAvailable(ToolId id) const
{
    return unavailableReason(id).isEmpty() && tool(id) != nullptr;
}

// Says why a tool cannot be used now, or an empty text when it can.
QString ToolController::unavailableReason(ToolId id) const
{
    if (id == ToolId::Select)
        return QString();
    if (tool(id) == nullptr)
        return tr("Not available yet");
    if (document_.isReadOnly())
        return tr("The project is read only");
    if (!hasImage())
        return tr("Open a plan image first");
    if (id != ToolId::Scale && !pixelsPerMetre().has_value())
        return tr("Set the scale first (S)");
    return QString();
}

// Falls back to Select when the active tool is no longer allowed, then tells the views.
void ToolController::refreshAvailability()
{
    if (!isAvailable(active_)) {
        tool(active_)->deactivate();
        active_ = ToolId::Select;
        canvas_->setTool(tool(active_));
        tool(active_)->activate();
        emit activeChanged(active_);
        hintChanged();
    }
    emit availabilityChanged();
    emit gridChanged();
}

// Passes a document change to the active tool.
void ToolController::documentChanged(unsigned changes)
{
    tool(active_)->documentChanged(changes);
}

// The one line hint of the active tool.
QString ToolController::hint() const
{
    return tool(active_)->hint();
}

// Tells the status bar the hint text may have changed.
void ToolController::hintChanged()
{
    emit hintTextChanged(hint());
}

// The first floor, which is the only one the app edits in this milestone.
const plynth::Floor* ToolController::floor() const
{
    const plynth::Plan& plan = document_.plan();
    return plan.floors.empty() ? nullptr : &plan.floors.front();
}

// Pixels per metre, when the scale is set.
std::optional<double> ToolController::pixelsPerMetre() const
{
    return document_.plan().scale.pixelsPerMetre;
}

// True when the first floor has an image on the sheet.
bool ToolController::hasImage() const
{
    return canvas_->hasImage();
}

// Turns the pixel snap radius of the theme into metres with the zoom and the scale.
double ToolController::snapRadiusMetres() const
{
    const std::optional<double> ppm = pixelsPerMetre();
    if (!ppm.has_value() || canvas_->zoom() <= 0.0)
        return 0.0;
    return Theme::instance().snapRadius() / canvas_->zoom() / *ppm;
}

// Turns snapping to the grid on or off.
void ToolController::setSnapToGrid(bool on)
{
    if (snapToGrid_ == on)
        return;
    snapToGrid_ = on;
    emit gridChanged();
}

// Keeps new tool settings and tells the panel and the tool.
void ToolController::setOpeningSettings(const OpeningToolSettings& settings)
{
    openingSettings_ = settings;
    emit openingSettingsChanged();
    if (active_ == ToolId::Opening)
        tool(ToolId::Opening)->documentChanged(0);
}

// Builds an opening from the tool settings and the project's default sizes for its type.
plynth::Opening ToolController::newOpening(const plynth::Id& wall, double offset) const
{
    plynth::Opening opening = plynth::defaultOpening(document_.project().settings.defaults, openingSettings_.type);
    opening.wall = wall;
    opening.offset = offset;
    opening.shape = openingSettings_.shape;
    opening.hinge = openingSettings_.hinge;
    opening.swing = openingSettings_.swing;
    return opening;
}

// Sets the thickness used for new walls.
void ToolController::setNewWallThickness(double metres)
{
    if (metres <= 0.0 || metres == newWallThickness_)
        return;
    newWallThickness_ = metres;
    emit newWallThicknessChanged(metres);
}

// The grid step on the plan image, shown only while the Wall tool is active and snapping to the grid.
double ToolController::gridStepPixels() const
{
    const std::optional<double> ppm = pixelsPerMetre();
    if (active_ != ToolId::Wall || !snapToGrid_ || !ppm.has_value())
        return 0.0;
    return document_.project().settings.gridSize * *ppm;
}

// Runs a command and shows its refusal as a message instead of a dialog.
plynth::Status ToolController::run(std::unique_ptr<plynth::Command> command)
{
    const plynth::Status status = document_.execute(std::move(command));
    if (!status.ok())
        showMessage(fromCore(status.message));
    return status;
}

// Shows a refusal in the status bar.
void ToolController::showMessage(const QString& text)
{
    emit message(text);
}

// Shows plain news in the status bar.
void ToolController::showNotice(const QString& text)
{
    emit notice(text);
}

}
