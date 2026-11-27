#pragma once

#include <plynth/core/Plan.h>

#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QTimer;
class QWidget;

namespace plynth {
class Document;
}

namespace plynth::app {

class MaterialPicker;
class MaterialRow;
class Selection;
class ToolController;

// The Canopy group of Properties for a selected canopy: its id, area, height and thickness, its roof with the
// fields its roof type uses, its three materials, and the problems the build would report about it. It adds its
// rows to the page's grid, and every edit runs SetCanopyCommand.
class CanopyGroup : public QObject {
    Q_OBJECT
public:
    CanopyGroup(plynth::Document& document, ToolController& controller, Selection& selection, QGridLayout* grid,
                QObject* parent = nullptr);

    // Shows the selected canopy. The build problems follow a moment later, so a drag stays smooth.
    void refresh();
    // The controls that are turned off while the project is read only.
    const std::vector<QWidget*>& editors() const { return editors_; }

private:
    enum class Slot { Covering, Underside, Fascia };

    const plynth::Canopy* selected() const;
    QDoubleSpinBox* addSpin(QGridLayout* grid, const QString& label, double min, double max, double step,
                            const QString& tip, QLabel** labelOut = nullptr);
    MaterialRow* addMaterial(QGridLayout* grid, const QString& label, const QString& tip, Slot slot);
    void edit(const std::function<void(plynth::Canopy&)>& change);
    void editRoof();
    void showMaterial(MaterialRow* row, const std::optional<std::string>& material);
    void openPicker(Slot slot, MaterialRow* row);
    void setMaterial(Slot slot, const std::optional<std::string>& material);
    void refreshProblems();

    plynth::Document& document_;
    ToolController& controller_;
    Selection& selection_;
    std::vector<QWidget*> editors_;
    bool refreshing_ = false;

    QLabel* id_ = nullptr;
    QLabel* area_ = nullptr;
    QLabel* corners_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QComboBox* type_ = nullptr;
    QLabel* pitchLabel_ = nullptr;
    QDoubleSpinBox* pitch_ = nullptr;
    QLabel* secondPitchLabel_ = nullptr;
    QDoubleSpinBox* secondPitch_ = nullptr;
    QLabel* breakRatioLabel_ = nullptr;
    QDoubleSpinBox* breakRatio_ = nullptr;
    QLabel* ridgeOffsetLabel_ = nullptr;
    QDoubleSpinBox* ridgeOffset_ = nullptr;
    QLabel* riseLabel_ = nullptr;
    QDoubleSpinBox* rise_ = nullptr;
    QCheckBox* autoDirection_ = nullptr;
    QLabel* directionLabel_ = nullptr;
    QDoubleSpinBox* direction_ = nullptr;
    QLabel* overhangLabel_ = nullptr;
    QDoubleSpinBox* overhang_ = nullptr;
    QLabel* parapetLabel_ = nullptr;
    QDoubleSpinBox* parapet_ = nullptr;
    MaterialRow* covering_ = nullptr;
    MaterialRow* underside_ = nullptr;
    MaterialRow* fascia_ = nullptr;
    MaterialPicker* picker_ = nullptr;
    Slot pickSlot_ = Slot::Covering;
    QLabel* problems_ = nullptr;
    QString shownId_;
    QTimer* problemTimer_ = nullptr;
};

}
