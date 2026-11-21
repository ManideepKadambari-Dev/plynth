#pragma once

#include <QDialog>

#include <optional>

class QCheckBox;
class QDoubleSpinBox;

namespace plynth::app {

// Asks for the real distance between the two points clicked with the Scale tool.
class ScaleDialog : public QDialog {
    Q_OBJECT
public:
    // presetMetres fills the box when a scale exists. showKeep shows the "Keep marks on the image" box.
    ScaleDialog(double pixelDistance, std::optional<double> presetMetres, bool showKeep, QWidget* parent = nullptr);

    double metres() const;
    bool keepMarksOnImage() const;

private:
    QDoubleSpinBox* distance_ = nullptr;
    QCheckBox* keep_ = nullptr;
};

}
