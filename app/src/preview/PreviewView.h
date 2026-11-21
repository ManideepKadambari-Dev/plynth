#pragma once

#include "ModelView.h"

#include <plynth/core/Plan.h>

#include <QWidget>

#include <filesystem>

class QLabel;
class QToolButton;

namespace plynth::app {

// The 3D page: the built building in a ModelView, with the Top and Three quarter buttons in the top right
// corner. When the graphics check failed it shows the reason and a link to the help page instead. View only.
class PreviewView : public QWidget {
    Q_OBJECT
public:
    explicit PreviewView(QWidget* parent = nullptr);

    // Loads project/build/building.fbx when it exists, else shows the empty state. The plan gives the bounds.
    void setProject(const std::filesystem::path& folder, const plynth::Plan& plan);
    // Loads the file again after a new build, with the bounds of the plan.
    void reload(const plynth::Plan& plan);
    // Frames the building.
    void fit();
    void setPreset(ModelView::Preset preset);
    ModelView::Preset preset() const;

    bool hasModel() const;
    // True when the scene shows nothing or has drawn its loaded model.
    bool pictureReady() const;
    ModelView* scene() const { return scene_; }
    // The path of the FBX of the open project, empty when there is no project folder.
    std::filesystem::path fbxPath() const { return fbx_; }

    // The box of a building in scene metres (X right, Y up, Z toward the front of the plan).
    static void buildingBounds(const plynth::Plan& plan, QVector3D* low, QVector3D* high);

signals:
    void presetChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void placeButtons();
    void showUnavailable(const QString& problem);
    void loadFile(const plynth::Plan& plan);

    ModelView* scene_ = nullptr;
    QLabel* unavailable_ = nullptr;
    QToolButton* topButton_ = nullptr;
    QToolButton* angleButton_ = nullptr;
    std::filesystem::path fbx_;
};

}
