#pragma once

#include "canvas/PlanCanvas.h"
#include "document/FloorFocus.h"
#include "document/Selection.h"
#include "tools/Tool.h"

#include <plynth/core/Document.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/Plan.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <QMetaObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class QWidget;

namespace plynth::app {

class DetectionSession;
class FurniturePicker;
class LibraryHost;

// What the Opening tool places: the type, the shape and, for doors, the hinge and swing. The sizes are the
// project's default sizes.
struct OpeningToolSettings {
    plynth::OpeningType type = plynth::OpeningType::Door;
    std::string shape = "rectangle";
    plynth::HingeEnd hinge = plynth::HingeEnd::Start;
    plynth::WallSide swing = plynth::WallSide::Right;
};

// The answers to the question asked when an item lands on another item: stand it on top of that item, keep it
// on the floor and stand the other item on it, or keep both on the floor.
enum class StackChoice { OnTop, Under, Floor };

// Owns the tools, says which one is active, and gives them the document, the selection and the canvas.
class ToolController : public QObject {
    Q_OBJECT
public:
    ToolController(plynth::Document& document, PlanCanvas* canvas, Selection* selection, QWidget* dialogParent,
                   QObject* parent = nullptr);
    ~ToolController() override;

    ToolId active() const { return active_; }
    // Switches tool. Does nothing when the tool is not available now.
    bool setActive(ToolId id);
    // Whether a tool can be used now, and when not, why.
    bool isAvailable(ToolId id) const;
    QString unavailableReason(ToolId id) const;
    // Re-checks availability after the plan or the read only state changed.
    void refreshAvailability();
    // The document changed. Forwards it to the active tool.
    void documentChanged(unsigned changes);

    QString hint() const;
    // For checks: switches to the Stair tool and shows the live preview of a drag from one place to another,
    // in metres. False when the tool is not available (no scale, or the shown floor is the top one).
    bool showStairPreview(double fromX, double fromY, double toX, double toY);

    plynth::Document& document() { return document_; }
    const plynth::Document& document() const { return document_; }
    PlanCanvas& canvas() { return *canvas_; }
    Selection& selection() { return *selection_; }
    QWidget* dialogParent() { return dialogParent_.data(); }
    DetectionSession& detection() { return *detection_; }
    // The library of assets, or null before the window gives it. catalog() is null while no library is open.
    void setLibrary(LibraryHost* library) { library_ = library; }
    LibraryHost* library() const { return library_; }
    plynth::AssetCatalog* catalog() const;
    // The furniture picker, made on first use over the canvas. Needs a library host.
    // over is the widget it is drawn over: the plan canvas when null, or the wall view.
    FurniturePicker& picker(QWidget* over = nullptr);
    bool pickerOpen() const;
    // For checks only: true when no picker is open or its preview has drawn its model.
    bool pickerPreviewReady() const;
    // Opens the picker over a widget at a point (in that widget) and calls chosen once with the asset the user
    // picks. Closing the picker without a choice calls nothing.
    void pickAsset(QWidget* over, const QPoint& anchor, const QString& roomType,
                   std::function<void(const QString&)> chosen);
    // Places a catalog asset on a wall of the shown floor as a wall item, flush on one face at an offset along
    // the wall, at the project's wall item centre height, and selects it. Refusals show as messages.
    bool placeWallAsset(const QString& assetId, const plynth::Id& wallId, plynth::WallSide side, double offset);
    // Snaps the centre of a floor item wanted at a place: to the grid and to nearby wall faces. Ctrl (free)
    // turns snapping off. Uses core's snapItemPosition with the project grid and the canvas snap radius.
    plynth::Vec2 snapItem(const plynth::Item& item, plynth::Vec2 wanted, bool free) const;
    // Places a catalog asset on the shown floor at a wanted place, snapped, and selects the new item. A
    // missing, broken or unmeasured asset is refused with a message. Returns true when placed.
    bool placeAsset(const QString& assetId, plynth::Vec2 wanted, bool free);
    // Places a generator from the library on the shown floor at a wanted place, snapped, with its default
    // values and the floor's ceiling height, and selects the new item. An unknown generator or one whose
    // add-on is off is refused with a message. Returns true when placed.
    bool placeGenerator(const QString& generatorId, plynth::Vec2 wanted, bool free);
    // Makes the item a generator item would be at the default values, for the footprint preview. Empty when
    // the generator is unknown.
    std::optional<plynth::Item> generatorItem(const QString& generatorId) const;
    // Rounds a light position to the grid, or leaves it when the grid is off or free is set (Ctrl).
    plynth::Vec2 snapLight(plynth::Vec2 wanted, bool free) const;
    // Adds a light marker of a kind on the shown floor at a place, snapped unless free, at the kind's start
    // height, and selects it. Refusals show as messages. Returns true when placed.
    bool placeLight(plynth::Vec2 wanted, plynth::LightKind kind, bool free);
    // Turns the selected floor item by degrees (positive is clockwise). Refusals show as messages.
    bool turnSelectedItem(double degrees);
    // The name shown for an item: the name of its asset or generator, or its asset id when the library does not
    // know it.
    QString itemName(const plynth::Item& item) const;
    // Asks where a dropped item goes when it lands on another item. Closing the question keeps both on the floor.
    StackChoice askStack(const plynth::Item& dropped, const plynth::Item& other);
    // Applies a stack choice between an item and another item of the shown floor as one undo step. other may be
    // empty for the Floor choice. Refusals show as messages.
    bool stackItem(const plynth::Id& item, const plynth::Id& other, StackChoice choice);
    // Called when a drag of an item ends: an item over a rug or another underlay goes on it by itself, and one
    // over another item asks with askStack. moved says the drag ran moves; the drag and the choice then become
    // one undo step.
    void settleDroppedItem(const plynth::Id& item, bool moved);
    // For checks: switches to the Furniture tool and opens its picker at a place in metres, then hovers an
    // asset when an id is given. False when the tool is not available.
    bool showFurniturePicker(double x, double y, const QString& hoverAsset);

    // The floor every tool works on: the shown one. Without a focus it is the first floor.
    void setFocus(FloorFocus* focus);
    const plynth::Floor* floor() const;
    // Redraws the overlay of the plan canvas for the shown floor. The window may call it instead of building
    // the overlay itself. It also runs on its own after floor and plan changes.
    void refreshOverlay();
    // Image alignment (View > Align plan image). While on, dragging the canvas moves the shown floor's plan
    // image and the arrow keys nudge it by 1 cm, or 10 cm with Shift. The offset change moves the marks too.
    bool alignImage() const { return alignImage_; }
    // True when the image cannot be aligned now.
    bool alignRefusal() const;
    std::optional<double> pixelsPerMetre() const;
    bool hasImage() const;
    // The snap radius in metres at the current zoom.
    double snapRadiusMetres() const;
    bool snapToGrid() const { return snapToGrid_; }
    void setSnapToGrid(bool on);
    const OpeningToolSettings& openingSettings() const { return openingSettings_; }
    void setOpeningSettings(const OpeningToolSettings& settings);
    // A new opening on a wall at an offset, with the tool settings and the project's default sizes. No id.
    plynth::Opening newOpening(const plynth::Id& wall, double offset) const;
    double newWallThickness() const { return newWallThickness_; }
    void setNewWallThickness(double metres);
    // Grid step in image pixels for the canvas, or 0 when no grid should show.
    double gridStepPixels() const;

    // Runs a command on the document. A refusal is shown as a message and returned.
    plynth::Status run(std::unique_ptr<plynth::Command> command);
    void showMessage(const QString& text);
    // Shows plain news in the status bar, such as what an accept added.
    void showNotice(const QString& text);
    // A new merge key. Every drag and every visit to an edit box takes its key from here, so commands from
    // different sources never merge into one undo step.
    int nextMergeKey() { return ++mergeKey_; }
    void requestTool(ToolId id) { setActive(id); }
    // Lets a tool say its hint changed.
    void hintChanged();

public slots:
    // Turns image alignment on or off. Refused with a message when the floor has no image or the scale is not set.
    void setAlignImage(bool on);

signals:
    void alignImageChanged(bool on);
    void activeChanged(ToolId id);
    void availabilityChanged();
    void hintTextChanged(const QString& text);
    void message(const QString& text);
    void notice(const QString& text);
    void gridChanged();
    void newWallThicknessChanged(double metres);
    void openingSettingsChanged();

private:
    Tool* tool(ToolId id) const;
    void floorChanged();
    // Places a new floor item on the shown floor and selects it. Stands it on an underlay under its centre by
    // itself, or asks first when it lands on another item.
    bool placeFloorItem(plynth::Item item);
    // Runs the place command for a new item after a stack choice with the item it landed on, and selects it.
    bool placeStacked(const plynth::Id& floorId, plynth::Item item, const plynth::Id& other, StackChoice choice);
    // The command for a stack choice between two items of the shown floor, or null when nothing would change.
    std::unique_ptr<plynth::Command> stackCommand(const plynth::Id& item, const plynth::Id& other,
                                                  StackChoice choice) const;
    // Runs SetFloorImageOffsetCommand to the floor's start offset plus a distance in metres.
    void moveImageTo(double dx, double dy);
    void nudgeImage(const QPoint& direction, bool big);

    plynth::Document& document_;
    QPointer<PlanCanvas> canvas_;
    QPointer<Selection> selection_;
    QPointer<QWidget> dialogParent_;
    std::array<std::unique_ptr<Tool>, kToolCount> tools_;
    OpeningToolSettings openingSettings_;
    int mergeKey_ = 0;
    FurniturePicker* picker_ = nullptr;
    QMetaObject::Connection pickChosen_;
    QMetaObject::Connection pickDismissed_;
    DetectionSession* detection_ = nullptr;
    LibraryHost* library_ = nullptr;
    ToolId active_ = ToolId::Select;
    QPointer<FloorFocus> focus_;
    bool alignImage_ = false;
    int alignKey_ = 0;
    plynth::ImageOffset alignFrom_;
    bool snapToGrid_ = true;
    double newWallThickness_ = 0.2;
};

}
