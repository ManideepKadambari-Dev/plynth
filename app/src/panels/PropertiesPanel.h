#pragma once

#include "tools/Tool.h"

#include <plynth/core/Generators.h>
#include <plynth/core/Materials.h>
#include <plynth/core/Roofs.h>

#include <QMetaObject>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;

namespace plynth {
class Command;
class Document;
struct LightStrip;
struct Stair;
}

namespace plynth::app {

class MaterialPicker;
class MaterialRow;
class OpeningAssetPicker;
class TrimGroup;
class VariationGroup;
class Selection;
class ToolController;

// The panel at the right of the canvas. It shows what is selected and lets the user edit it, and runs
// every edit as a command through the tool controller.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(plynth::Document& document, Selection& selection, ToolController& controller,
                    QWidget* parent = nullptr);

    // Reads everything shown from the document and the selection.
    void refresh();
    // The item actions of the Properties page, also run from the Edit menu on the selected item.
    void swapItemAsset();
    void mountItem();
    void takeOffItem();
    void updateItemSize();
    // For checks only: selects what a material target such as "w3.outside" or "default.floor" names and opens the
    // material picker on its row. With a material id, hovers it. False when the target is unknown or not shown.
    // With pickMaterial, chooses that material at once as a click would.
    bool openMaterialPicker(const QString& target, const QString& hoverMaterial, const QString& pickMaterial = QString());
    // For checks only: sets one parameter of the selected generator item as a spin box visit would, from text such
    // as "diameter=0.5". All calls before the next focus change share one undo step. False with the reason in
    // message when it is refused.
    bool setGeneratorParamText(const QString& assignment, QString* message);
    // For checks only: opens the door and window picker of the selected opening and hovers an asset when one is
    // given. False when no door or window is selected.
    bool showOpeningAssetPicker(const QString& hoverAsset);
    // For checks only: scrolls the project page so the Trim group is at the top.
    void scrollToFinish();
    // For checks only: true when the picker is closed or its preview has its picture.
    bool pickerOpen() const;

signals:
    void deleteRequested();
    // The Choose image button of the Floor section was pressed.
    void planImageRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* makePage(QGridLayout** grid);
    QLabel* addLabelRow(QGridLayout* grid, const QString& label, bool mono = true, bool elide = false);
    QDoubleSpinBox* addSpinRow(QGridLayout* grid, const QString& label, double min, double max, double step,
                               const QString& tip, QLabel** labelOut = nullptr);
    void buildNothingPage();
    void buildPointPage();
    void buildWallPage();
    void buildRoomPage();
    void buildDetectPage();
    void buildOpeningPage();
    void buildOpeningToolPage();
    void buildItemPage();
    void buildStairPage();
    void buildLightPage();
    void refreshLight();
    void editLight(bool place);
    void buildLightStripPage();
    void refreshLightStrip();
    void editLightStrip(const std::function<void(plynth::LightStrip&)>& change, bool merge);
    void openAssetPicker();
    void refreshStair();
    void editStair(const std::function<void(plynth::Stair&)>& change);
    void buildFloorSection(QGridLayout* grid);
    void refreshUnusedRoofEntries();
    // The widgets of one roof settings group: the Roof default on the project page and the page of a section.
    struct RoofEditor {
        QComboBox* type = nullptr;
        QLabel* pitchLabel = nullptr;
        QDoubleSpinBox* pitch = nullptr;
        QCheckBox* autoDirection = nullptr;
        QLabel* directionLabel = nullptr;
        QDoubleSpinBox* direction = nullptr;
        QLabel* overhangLabel = nullptr;
        QDoubleSpinBox* overhang = nullptr;
        QLabel* parapetLabel = nullptr;
        QDoubleSpinBox* parapet = nullptr;
    };
    void buildRoofEditor(QGridLayout* grid, RoofEditor& editor, bool section);
    void fillRoofEditor(const RoofEditor& editor, const plynth::RoofSettings& settings);
    plynth::RoofSettings readRoofEditor(const RoofEditor& editor) const;
    void buildRoofPage();
    void refreshRoof();
    void editRoofDefault();
    void editRoofSection(const std::function<void(plynth::RoofSettings&, std::optional<std::string>&)>& change);
    std::optional<plynth::RoofArea> selectedRoof() const;
    plynth::ResolvedMaterial resolveMaterial(const plynth::MaterialTarget& target) const;
    void editImageOffset(bool xChanged);
    void refreshItem();
    void buildGeneratorBody(const plynth::Item& item, const plynth::GeneratorInfo* info);
    void refreshGenerator(const plynth::Item& item, const plynth::GeneratorInfo* info, bool changed);
    void editGeneratorParam(const std::string& paramId, const plynth::GeneratorValue& value);
    void resetGenerator();
    const plynth::Item* selectedGeneratorItem() const;
    MaterialPicker* ensurePicker();
    void updateGenerator();
    void runItemCommand(std::unique_ptr<plynth::Command> command);
    void refreshOpening();
    void refreshOpeningTool();
    QComboBox* addComboRow(QGridLayout* grid, const QString& label, const QString& tip, QLabel** labelOut = nullptr);
    void fillShapeCombo(QComboBox* combo, const std::string& current);
    void runOpeningCommand(std::unique_ptr<plynth::Command> command);
    void editOpeningStyle(int field, int index);
    void editToolSetting(int field, int index);
    void editDefaultSize(int field, double value);
    QSlider* addSliderRow(QGridLayout* grid, const QString& label, int min, int max, const QString& tip,
                          QLabel** valueOut);
    void detectSettingsEdited(const QObject* from);
    void refreshNothing();
    void refreshPoint();
    void refreshWall();
    void refreshRoom();
    void refreshDetect();
    void setSpin(QDoubleSpinBox* spin, double value);
    QDoubleSpinBox* addKeyedSpinRow(QGridLayout* grid, const QString& label, double min, double max, double step,
                                    const QString& tip);
    QLabel* addNote(QGridLayout* grid);
    void addMaterialSection(QGridLayout* grid, const QString& title);
    MaterialRow* addMaterialRow(QGridLayout* grid, const QString& label, const QString& tip, const plynth::MaterialTarget& target);
    void refreshMaterialRows(int page);
    void openPicker(size_t slot, const QString& hoverMaterial);
    void applyMaterial(const plynth::MaterialTarget& target, const std::optional<std::string>& material);
    // One material setting: its row, the part of its target that does not depend on the selection, and its page.
    struct MaterialSlot {
        MaterialRow* row = nullptr;
        plynth::MaterialTarget target;
        int page = 0;
    };

    plynth::Document& document_;
    Selection& selection_;
    ToolController& controller_;
    bool refreshing_ = false;
    bool gridEdited_ = false;
    bool pointXEdited_ = false;
    bool pointYEdited_ = false;

    QStackedWidget* pages_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    std::vector<QWidget*> editors_;
    std::vector<MaterialSlot> materialSlots_;
    MaterialPicker* picker_ = nullptr;
    plynth::MaterialTarget pickTarget_;

    QLabel* scaleValue_ = nullptr;
    QLabel* scaleHint_ = nullptr;
    QDoubleSpinBox* gridSpin_ = nullptr;
    QComboBox* exportModeCombo_ = nullptr;
    QLabel* pointCount_ = nullptr;
    QLabel* wallCount_ = nullptr;
    QLabel* roomCount_ = nullptr;
    QLabel* newWallLabel_ = nullptr;
    QDoubleSpinBox* newWallSpin_ = nullptr;

    QLabel* pointId_ = nullptr;
    QDoubleSpinBox* pointX_ = nullptr;
    QDoubleSpinBox* pointY_ = nullptr;
    QLabel* pointWalls_ = nullptr;
    QDoubleSpinBox* cornerRadius_ = nullptr;
    QLabel* cornerNote_ = nullptr;

    QLabel* wallId_ = nullptr;
    QLabel* wallFrom_ = nullptr;
    QLabel* wallTo_ = nullptr;
    QLabel* wallLength_ = nullptr;
    QDoubleSpinBox* wallThickness_ = nullptr;
    QDoubleSpinBox* wallBend_ = nullptr;
    QLabel* wallOpenings_ = nullptr;
    QCheckBox* wallOwnHeight_ = nullptr;
    QDoubleSpinBox* wallHeight_ = nullptr;
    QDoubleSpinBox* ceilingSpin_ = nullptr;

    QLabel* openingId_ = nullptr;
    QLabel* openingWall_ = nullptr;
    QLabel* openingProblem_ = nullptr;
    QComboBox* openingType_ = nullptr;
    QComboBox* openingShape_ = nullptr;
    QComboBox* openingHinge_ = nullptr;
    QComboBox* openingSwing_ = nullptr;
    QLabel* openingHingeLabel_ = nullptr;
    QLabel* openingSwingLabel_ = nullptr;
    QDoubleSpinBox* openingOffset_ = nullptr;
    QDoubleSpinBox* openingSill_ = nullptr;
    QDoubleSpinBox* openingWidth_ = nullptr;
    QDoubleSpinBox* openingHeight_ = nullptr;
    QLabel* openingAssetLabel_ = nullptr;
    QPushButton* openingAsset_ = nullptr;
    OpeningAssetPicker* assetPicker_ = nullptr;

    QLabel* itemThumb_ = nullptr;
    QLabel* itemName_ = nullptr;
    QLabel* itemAsset_ = nullptr;
    QLabel* itemSize_ = nullptr;
    QLabel* itemRoom_ = nullptr;
    QLabel* itemWall_ = nullptr;
    QLabel* itemState_ = nullptr;
    QLabel* itemProblems_ = nullptr;
    QPushButton* itemUpdate_ = nullptr;
    QPushButton* itemSwap_ = nullptr;
    QPushButton* itemMount_ = nullptr;
    QPushButton* itemTakeOff_ = nullptr;
    QPushButton* itemTurnLeft_ = nullptr;
    QPushButton* itemTurnRight_ = nullptr;
    struct GeneratorRow {
        std::string id;
        plynth::GeneratorParamType type = plynth::GeneratorParamType::Number;
        QWidget* editor = nullptr;
    };
    QWidget* genGroup_ = nullptr;
    QWidget* genBody_ = nullptr;
    QString genKey_;
    std::vector<GeneratorRow> genRows_;
    QLabel* genName_ = nullptr;
    QLabel* genDescription_ = nullptr;
    QLabel* genSize_ = nullptr;
    QLabel* genNote_ = nullptr;
    QPushButton* genReset_ = nullptr;
    QPushButton* genUpdate_ = nullptr;
    int genFocusKey_ = 0;
    std::string pickGeneratorParam_;
    std::vector<QWidget*> assetOnly_;
    QDoubleSpinBox* itemX_ = nullptr;
    QDoubleSpinBox* itemY_ = nullptr;
    QDoubleSpinBox* itemRotation_ = nullptr;
    QDoubleSpinBox* itemZ_ = nullptr;
    QDoubleSpinBox* itemCentre_ = nullptr;
    std::vector<QWidget*> floorOnly_;
    std::vector<QWidget*> wallOnly_;
    QDoubleSpinBox* wallCentreSpin_ = nullptr;

    QComboBox* toolType_ = nullptr;
    QComboBox* toolShape_ = nullptr;
    QComboBox* toolHinge_ = nullptr;
    QComboBox* toolSwing_ = nullptr;
    QLabel* toolHingeLabel_ = nullptr;
    QLabel* toolSwingLabel_ = nullptr;
    QDoubleSpinBox* toolWidth_ = nullptr;
    QDoubleSpinBox* toolHeight_ = nullptr;
    QDoubleSpinBox* toolSill_ = nullptr;
    QLabel* toolSillLabel_ = nullptr;
    QPushButton* straightenButton_ = nullptr;
    int focusKey_ = 0;
    std::vector<QDoubleSpinBox*> keyedSpins_;

    QLineEdit* floorName_ = nullptr;
    QDoubleSpinBox* slabSpin_ = nullptr;
    QLabel* floorImage_ = nullptr;
    QDoubleSpinBox* offsetX_ = nullptr;
    QDoubleSpinBox* offsetY_ = nullptr;
    QCheckBox* moveMarks_ = nullptr;

    QLabel* stairId_ = nullptr;
    QLabel* stairFloors_ = nullptr;
    QComboBox* stairType_ = nullptr;
    QDoubleSpinBox* stairWidth_ = nullptr;
    QComboBox* stairTurn_ = nullptr;
    QLabel* stairTurnLabel_ = nullptr;
    QLabel* stairRise_ = nullptr;
    QLabel* stairRisers_ = nullptr;
    QLabel* stairRiser_ = nullptr;
    QLabel* stairGoing_ = nullptr;
    QLabel* stairLength_ = nullptr;
    QLabel* stairProblems_ = nullptr;

    RoofEditor roofDefault_;
    QWidget* unusedRoofBox_ = nullptr;
    std::string shownFloorId_;
    RoofEditor roofSection_;
    QLabel* roofFloor_ = nullptr;
    QLabel* roofArea_ = nullptr;
    QLabel* roofEntry_ = nullptr;
    QPushButton* roofUseDefault_ = nullptr;

    QLabel* roomId_ = nullptr;
    QComboBox* roomType_ = nullptr;
    QLabel* roomArea_ = nullptr;
    QLabel* roomWalls_ = nullptr;
    QCheckBox* roomDressing_ = nullptr;

    TrimGroup* trimGroup_ = nullptr;
    VariationGroup* variationGroup_ = nullptr;

    QLabel* lightId_ = nullptr;
    QLabel* lightRoom_ = nullptr;
    QComboBox* lightKind_ = nullptr;
    QDoubleSpinBox* lightHeight_ = nullptr;
    QDoubleSpinBox* lightX_ = nullptr;
    QDoubleSpinBox* lightY_ = nullptr;

    QLabel* stripId_ = nullptr;
    QComboBox* stripKind_ = nullptr;
    QLabel* stripHeightLabel_ = nullptr;
    QDoubleSpinBox* stripHeight_ = nullptr;
    QDoubleSpinBox* stripPower_ = nullptr;
    QLabel* stripTotalLabel_ = nullptr;
    QLabel* stripTotal_ = nullptr;
    QLabel* stripLineLabel_ = nullptr;
    QLabel* stripLine_ = nullptr;
    QLabel* stripStairLabel_ = nullptr;
    QLabel* stripStair_ = nullptr;
    QLabel* stripProblems_ = nullptr;

    QSlider* thinSlider_ = nullptr;
    QLabel* thinValue_ = nullptr;
    QSlider* thickSlider_ = nullptr;
    QLabel* thickValue_ = nullptr;
    QSlider* sensitivitySlider_ = nullptr;
    QLabel* sensitivityValue_ = nullptr;
    QDoubleSpinBox* lengthSpin_ = nullptr;
    QDoubleSpinBox* openingSpin_ = nullptr;
    QCheckBox* roomsCheck_ = nullptr;
    QCheckBox* openingsCheck_ = nullptr;
    QLabel* detectorLabel_ = nullptr;
    QComboBox* detectorCombo_ = nullptr;
    QLabel* detectStatus_ = nullptr;
    QPushButton* acceptButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
};

}
