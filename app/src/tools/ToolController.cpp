#include "ToolController.h"

#include "canvas/PlanCanvas.h"
#include "catalog/LibraryHost.h"
#include "document/FloorFocus.h"
#include "canvas/OverlayBuilder.h"
#include "document/Selection.h"
#include "detection/DetectionSession.h"
#include "theme/Theme.h"
#include "furniture/FurniturePicker.h"
#include "tools/DetectTool.h"
#include "tools/FurnitureTool.h"
#include "tools/OpeningTool.h"
#include "tools/RoomTool.h"
#include "tools/ScaleTool.h"
#include "tools/SelectTool.h"
#include "tools/WallTool.h"

#include <plynth/core/Items.h>
#include <plynth/core/Openings.h>
#include <plynth/core/Snapping.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/catalog/ItemAssets.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/ItemCommands.h>

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
    tools_[static_cast<size_t>(ToolId::Furniture)] = std::make_unique<FurnitureTool>(*this);
    detection_ = new DetectionSession(*this, this);
    tools_[static_cast<size_t>(ToolId::Detect)] = std::make_unique<DetectTool>(*this);
    connect(detection_, &DetectionSession::changed, this, [this]() {
        if (canvas_ != nullptr)
            canvas_->setProposals(detection_->overlay());
    });
    canvas_->setTool(tool(active_));
    tool(active_)->activate();
    if (parent != nullptr)
        setFocus(parent->findChild<FloorFocus*>());
    connect(canvas_, &PlanCanvas::alignDragStarted, this, [this]() {
        if (const plynth::Floor* current = floor()) {
            alignFrom_ = current->imageOffset;
            alignKey_ = nextMergeKey();
        }
    });
    connect(canvas_, &PlanCanvas::alignDragged, this, [this](const QPointF& delta) {
        const std::optional<double> ppm = pixelsPerMetre();
        if (ppm.has_value() && *ppm > 0.0)
            moveImageTo(alignFrom_.x + delta.x() / *ppm, alignFrom_.y + delta.y() / *ppm);
    });
    connect(canvas_, &PlanCanvas::alignNudged, this, [this](const QPoint& direction, bool big) {
        nudgeImage(direction, big);
    });
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
    if (id == ToolId::Furniture && catalog() == nullptr)
        return tr("No library folder is set.");
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
    constexpr unsigned overlayChanges = plynth::Change::Content | plynth::Change::FloorImage |
                                        plynth::Change::Scale | plynth::Change::Floors | plynth::Change::Folder;
    if (changes & overlayChanges)
        refreshOverlay();
    tool(active_)->documentChanged(changes);
}

// Draws the shown floor over the plan: its marks, the faint floor below, and the image at its offset.
void ToolController::refreshOverlay()
{
    const std::string id = focus_ != nullptr ? focus_->floorId().toStdString() : std::string();
    canvas_->setOverlay(id.empty() ? buildOverlay(document_.plan(), catalog()) : buildOverlay(document_.plan(), id, catalog()));
    canvas_->setGridStep(gridStepPixels());
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

// Follows the shown floor. Tools and detection start fresh when it changes.
void ToolController::setFocus(FloorFocus* focus)
{
    if (focus_ != nullptr)
        disconnect(focus_, nullptr, this, nullptr);
    focus_ = focus;
    if (focus_ != nullptr)
        connect(focus_, &FloorFocus::changed, this, [this]() { floorChanged(); });
}

// The shown floor, or the first one when no focus was given.
const plynth::Floor* ToolController::floor() const
{
    if (focus_ != nullptr)
        return focus_->floor();
    const plynth::Plan& plan = document_.plan();
    return plan.floors.empty() ? nullptr : &plan.floors.front();
}

// Another floor is shown: alignment ends, the active tool and detection drop their state.
void ToolController::floorChanged()
{
    setAlignImage(false);
    tool(active_)->floorChanged();
    detection_->floorChanged();
    refreshOverlay();
    refreshAvailability();
}

// True when the image cannot be aligned now: read only, no image on this floor, or no scale.
bool ToolController::alignRefusal() const
{
    return document_.isReadOnly() || floor() == nullptr || !floor()->image.has_value() || !hasImage() ||
           !pixelsPerMetre().has_value();
}

// Turns alignment on or off. Turning on is refused without an image, a scale or write access.
void ToolController::setAlignImage(bool on)
{
    if (on && alignRefusal()) {
        if (document_.isReadOnly())
            showMessage(tr("The project is read only"));
        else if (!pixelsPerMetre().has_value())
            showMessage(tr("Set the scale first (S)"));
        else
            showMessage(tr("This floor has no plan image to align"));
        on = false;
    }
    if (on == alignImage_)
        return;
    alignImage_ = on;
    canvas_->setAlignMode(on);
    emit alignImageChanged(on);
    if (on)
        showNotice(tr("Drag the plan image to line it up. Arrow keys nudge it by 1 cm, Shift by 10 cm."));
}

// Moves the shown floor's image offset to a place. Moves in one drag share a merge key, so they undo as one.
void ToolController::moveImageTo(double dx, double dy)
{
    const plynth::Floor* current = floor();
    if (current == nullptr)
        return;
    run(std::make_unique<plynth::SetFloorImageOffsetCommand>(current->id, plynth::ImageOffset{dx, dy}, true,
                                                             alignKey_));
}

// Moves the image by 1 cm, or 10 cm when big. Each key press is its own undo step.
void ToolController::nudgeImage(const QPoint& direction, bool big)
{
    const plynth::Floor* current = floor();
    if (current == nullptr)
        return;
    const double step = big ? 0.10 : 0.01;
    run(std::make_unique<plynth::SetFloorImageOffsetCommand>(
        current->id,
        plynth::ImageOffset{current->imageOffset.x + direction.x() * step, current->imageOffset.y + direction.y() * step},
        true, nextMergeKey()));
}

// Pixels per metre, when the scale is set.
std::optional<double> ToolController::pixelsPerMetre() const
{
    return document_.plan().scale.pixelsPerMetre;
}

// True when the shown floor has an image on the sheet.
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

// The open catalog, or null when no library is open.
plynth::AssetCatalog* ToolController::catalog() const
{
    return library_ != nullptr ? library_->catalog() : nullptr;
}

// The furniture picker, a child of the canvas so it draws over the plan and in screenshots.
FurniturePicker& ToolController::picker(QWidget* over)
{
    QWidget* parent = over != nullptr ? over : static_cast<QWidget*>(canvas_.data());
    if (picker_ == nullptr)
        picker_ = new FurniturePicker(*library_, parent);
    else if (picker_->parentWidget() != parent)
        picker_->setParent(parent);
    return *picker_;
}

// Opens the picker for one choice. The connections end when the choice is made or the picker closes.
void ToolController::pickAsset(QWidget* over, const QPoint& anchor, const QString& roomType,
                               std::function<void(const QString&)> chosen)
{
    if (library_ == nullptr || catalog() == nullptr)
        return;
    FurniturePicker& shown = picker(over);
    QObject::disconnect(pickChosen_);
    QObject::disconnect(pickDismissed_);
    pickChosen_ = connect(&shown, &FurniturePicker::assetChosen, this, [this, chosen](const QString& id) {
        QObject::disconnect(pickChosen_);
        QObject::disconnect(pickDismissed_);
        chosen(id);
    });
    pickDismissed_ = connect(&shown, &FurniturePicker::dismissed, this, [this]() {
        QObject::disconnect(pickChosen_);
        QObject::disconnect(pickDismissed_);
    });
    shown.openAt(anchor, roomType);
}

// Checks the asset, works out the flush pose on the wall face and runs PlaceItemCommand.
bool ToolController::placeWallAsset(const QString& assetId, const plynth::Id& wallId, plynth::WallSide side,
                                    double offset)
{
    const plynth::Floor* current = floor();
    plynth::AssetCatalog* assets = catalog();
    if (current == nullptr)
        return false;
    if (assets == nullptr) {
        showMessage(tr("No library folder is set."));
        return false;
    }
    const plynth::Wall* wall = plynth::findWall(*current, wallId);
    if (wall == nullptr)
        return false;
    const plynth::ItemAsset asset = plynth::itemAssetFor(*assets, assetId.toStdString());
    if (!asset.status.ok()) {
        showMessage(fromCore(asset.status.message));
        return false;
    }
    const plynth::ItemPose pose = plynth::wallItemPose(*current, *wall, side, offset, asset.size);
    if (!pose.status.ok()) {
        showMessage(fromCore(pose.status.message));
        return false;
    }
    plynth::Item item;
    item.asset = assetId.toStdString();
    item.size = asset.size;
    item.x = pose.position.x;
    item.y = pose.position.y;
    item.rotation = pose.rotation;
    item.wall = wallId;
    item.z = plynth::defaultWallItemZ(document_.project().settings.defaults, asset.size.height);
    auto command = std::make_unique<plynth::PlaceItemCommand>(current->id, item);
    const plynth::PlaceItemCommand* raw = command.get();
    if (!run(std::move(command)).ok())
        return false;
    selection_->set(SelectionKind::Item, QString::fromStdString(raw->itemId()));
    return true;
}

// True while the picker is showing.
bool ToolController::pickerOpen() const
{
    return picker_ != nullptr && picker_->isOpen();
}

// Rounds the wanted centre to the grid and slides the footprint flush to wall faces near it.
plynth::Vec2 ToolController::snapItem(const plynth::Item& item, plynth::Vec2 wanted, bool free) const
{
    const plynth::Floor* current = floor();
    if (current == nullptr)
        return wanted;
    plynth::ItemSnapOptions options;
    if (!free) {
        if (snapToGrid_)
            options.gridSize = document_.project().settings.gridSize;
        options.radius = snapRadiusMetres();
    }
    return plynth::snapItemPosition(*current, item, wanted, options).position;
}

// Checks the asset, snaps the place, runs PlaceItemCommand and selects the new item.
bool ToolController::placeAsset(const QString& assetId, plynth::Vec2 wanted, bool free)
{
    const plynth::Floor* current = floor();
    plynth::AssetCatalog* assets = catalog();
    if (current == nullptr)
        return false;
    if (assets == nullptr) {
        showMessage(tr("No library folder is set."));
        return false;
    }
    const plynth::ItemAsset asset = plynth::itemAssetFor(*assets, assetId.toStdString());
    if (!asset.status.ok()) {
        showMessage(fromCore(asset.status.message));
        return false;
    }
    plynth::Item item;
    item.asset = assetId.toStdString();
    item.size = asset.size;
    const plynth::Vec2 at = snapItem(item, wanted, free);
    item.x = at.x;
    item.y = at.y;
    auto command = std::make_unique<plynth::PlaceItemCommand>(current->id, item);
    const plynth::PlaceItemCommand* raw = command.get();
    if (!run(std::move(command)).ok())
        return false;
    selection_->set(SelectionKind::Item, QString::fromStdString(raw->itemId()));
    return true;
}

// Turns the selected floor item with RotateItemCommand. Each key press is its own undo step.
bool ToolController::turnSelectedItem(double degrees)
{
    const plynth::Floor* current = floor();
    if (current == nullptr || selection_->kind() != SelectionKind::Item || document_.isReadOnly())
        return false;
    const plynth::Item* item = plynth::findItem(*current, selection_->id().toStdString());
    if (item == nullptr)
        return false;
    return run(std::make_unique<plynth::RotateItemCommand>(item->id, plynth::normalizeRotation(item->rotation + degrees))).ok();
}

// True when the picker is not open or its preview has no model waiting.
bool ToolController::pickerPreviewReady() const
{
    return picker_ == nullptr || !picker_->isVisible() || picker_->previewReady();
}

// Switches to the Furniture tool and opens its picker at a place, anchored where that place is on screen.
bool ToolController::showFurniturePicker(double x, double y, const QString& hoverAsset)
{
    const std::optional<double> ppm = pixelsPerMetre();
    if (!ppm.has_value() || !setActive(ToolId::Furniture))
        return false;
    auto* furniture = static_cast<FurnitureTool*>(tool(ToolId::Furniture));
    const QPoint view = canvas_->viewport()->mapTo(canvas_, canvas_->sceneToView(QPointF(x * *ppm, y * *ppm)).toPoint());
    furniture->openAt(plynth::Vec2{x, y}, view);
    if (!hoverAsset.isEmpty())
        furniture->hover(hoverAsset);
    return true;
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
