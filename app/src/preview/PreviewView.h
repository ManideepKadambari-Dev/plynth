#pragma once

#include "ModelView.h"

#include <plynth/core/Plan.h>

#include <QWidget>

#include <filesystem>
#include <string>
#include <vector>

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
    // Hides the roofs when showRoofs is false. A non empty cutFloorId shows that floor and the ones below it
    // only, with no roofs. Kept across builds and project loads, and ignored for a build without parts.
    void setRoofOptions(bool showRoofs, const QString& cutFloorId);
    bool showRoofs() const { return showRoofs_; }
    QString cutFloorId() const { return cutFloorId_; }
    // The parts of the build in fbx's folder as scene parts, floors named by their place in floorIds. Empty
    // when there is no parts.json or none of its files is there.
    static QList<ModelView::Part> readParts(const std::filesystem::path& fbx, const std::vector<std::string>& floorIds);
    void setPreset(ModelView::Preset preset);
    ModelView::Preset preset() const;

    bool hasModel() const;
    // True when the scene shows nothing or has drawn its loaded model.
    bool pictureReady() const;
    ModelView* scene() const { return scene_; }
    // The path of the FBX of the open project, empty when there is no project folder.
    std::filesystem::path fbxPath() const { return fbx_; }

    // The box of a building in scene metres (X right, Y up, Z toward the front of the plan).
    // lastFloor limits it to the floors up to that index (-1 for all) and withRoofs adds the roofs.
    static void buildingBounds(const plynth::Plan& plan, QVector3D* low, QVector3D* high, int lastFloor = -1,
                               bool withRoofs = true);

signals:
    void presetChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void placeButtons();
    void showUnavailable(const QString& problem);
    void loadFile(const plynth::Plan& plan);
    void applyFilter();

    ModelView* scene_ = nullptr;
    QLabel* unavailable_ = nullptr;
    QToolButton* topButton_ = nullptr;
    QToolButton* angleButton_ = nullptr;
    std::filesystem::path fbx_;
    bool showRoofs_ = true;
    QString cutFloorId_;
    std::vector<std::string> floorIds_;
    QVector3D low_;
    QVector3D high_;
    std::vector<QVector3D> cutHigh_;
};

}
