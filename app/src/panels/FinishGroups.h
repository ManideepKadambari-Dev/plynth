#pragma once

#include <plynth/core/FinishSettings.h>

#include <QObject>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSpinBox;
class QWidget;

namespace plynth {
class Document;
}

namespace plynth::app {

class ToolController;

// The Trim group of the project page: for each kind of trim a switch, a profile, a height and a depth, then the
// railing height. It adds its rows to the page's grid, and every edit runs SetTrimCommand.
class TrimGroup : public QObject {
    Q_OBJECT
public:
    TrimGroup(plynth::Document& document, ToolController& controller, QGridLayout* grid, QObject* parent = nullptr);

    // Shows the project's trim settings and the registered profiles.
    void refresh();
    // The controls that are turned off while the project is read only.
    const std::vector<QWidget*>& editors() const { return editors_; }
    // The title of the group, to scroll to.
    QWidget* title() const { return title_; }

private:
    struct KindRow {
        plynth::TrimKind kind = plynth::TrimKind::Skirting;
        QCheckBox* on = nullptr;
        QComboBox* profile = nullptr;
        QDoubleSpinBox* height = nullptr;
        QDoubleSpinBox* depth = nullptr;
    };
    void addKind(QGridLayout* grid, plynth::TrimKind kind, const QString& name, const QString& tip);
    void edit(const KindRow& row);

    plynth::Document& document_;
    ToolController& controller_;
    std::vector<KindRow> rows_;
    QDoubleSpinBox* railingHeight_ = nullptr;
    QWidget* title_ = nullptr;
    std::vector<QWidget*> editors_;
    bool refreshing_ = false;
};

// The Variation group of the project page: the seed, the furniture and material switches and Randomize. Every
// edit runs SetVariationCommand.
class VariationGroup : public QObject {
    Q_OBJECT
public:
    VariationGroup(plynth::Document& document, ToolController& controller, QGridLayout* grid,
                   QObject* parent = nullptr);

    // Shows the project's variation settings.
    void refresh();
    const std::vector<QWidget*>& editors() const { return editors_; }

private:
    void edit(plynth::VariationSettings settings);

    plynth::Document& document_;
    ToolController& controller_;
    QSpinBox* seed_ = nullptr;
    QCheckBox* furniture_ = nullptr;
    QCheckBox* materials_ = nullptr;
    QPushButton* randomize_ = nullptr;
    QLabel* note_ = nullptr;
    std::vector<QWidget*> editors_;
    bool refreshing_ = false;
};

}
