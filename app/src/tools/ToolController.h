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

    plynth::Document& document() { return document_; }
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
    // Turns the selected floor item by degrees (positive is clockwise). Refusals show as messages.
    bool turnSelectedItem(double degrees);
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
    // Runs SetFloorImageOffsetCommand to the floor's start offset plus a distance in metres.
    void moveImageTo(double dx, double dy);
    void nudgeImage(const QPoint& direction, bool big);

    plynth::Document& document_;
    QPointer<PlanCanvas> canvas_;
    QPointer<Selection> selection_;
    QPointer<QWidget> dialogParent_;
    std::array<std::unique_ptr<Tool>, kAllTools.size()> tools_;
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
