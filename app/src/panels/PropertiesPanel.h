#pragma once

#include "tools/Tool.h"

#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace plynth {
class Document;
}

namespace plynth::app {

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

signals:
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* makePage(QGridLayout** grid);
    QLabel* addLabelRow(QGridLayout* grid, const QString& label, bool mono = true);
    QDoubleSpinBox* addSpinRow(QGridLayout* grid, const QString& label, double min, double max, double step,
                               const QString& tip, const QString& tourId, QLabel** labelOut = nullptr);
    void buildNothingPage();
    void buildPointPage();
    void buildWallPage();
    void buildRoomPage();
    void buildDetectPage();
    QSlider* addSliderRow(QGridLayout* grid, const QString& label, int min, int max, const QString& tip,
                          const QString& tourId, QLabel** valueOut);
    void detectSettingsEdited(const QObject* from);
    void refreshNothing();
    void refreshPoint();
    void refreshWall();
    void refreshRoom();
    void refreshDetect();
    void setSpin(QDoubleSpinBox* spin, double value);
    QDoubleSpinBox* addKeyedSpinRow(QGridLayout* grid, const QString& label, double min, double max, double step,
                                    const QString& tip, const QString& tourId);
    QLabel* addNote(QGridLayout* grid);

    plynth::Document& document_;
    Selection& selection_;
    ToolController& controller_;
    bool refreshing_ = false;
    bool gridEdited_ = false;
    bool pointXEdited_ = false;
    bool pointYEdited_ = false;

    QStackedWidget* pages_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    std::vector<QWidget*> editors_;

    QLabel* scaleValue_ = nullptr;
    QLabel* scaleHint_ = nullptr;
    QDoubleSpinBox* gridSpin_ = nullptr;
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
    QPushButton* straightenButton_ = nullptr;
    int focusKey_ = 1;
    std::vector<QDoubleSpinBox*> keyedSpins_;

    QLabel* roomId_ = nullptr;
    QComboBox* roomType_ = nullptr;
    QLabel* roomArea_ = nullptr;
    QLabel* roomWalls_ = nullptr;

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
