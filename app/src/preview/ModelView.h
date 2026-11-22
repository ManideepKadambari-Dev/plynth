#pragma once

#include <QQuickWidget>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector3D>

#include <filesystem>

namespace plynth::app {

class ThemeQml;

// One 3D scene in a widget: a model loaded from an FBX file, lit, with a camera that orbits it. It is the
// QML scene of app/qml/BuildScene.qml and only shows things, it never changes the plan.
class ModelView : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QString modelFile READ modelFileText NOTIFY modelFileChanged)
    Q_PROPERTY(QVariantList parts READ partUrls NOTIFY partsChanged)
    Q_PROPERTY(QVariantList partVisible READ partVisible NOTIFY partVisibleChanged)
    Q_PROPERTY(QVector3D boundsMin READ boundsMin NOTIFY boundsChanged)
    Q_PROPERTY(QVector3D boundsMax READ boundsMax NOTIFY boundsChanged)
    Q_PROPERTY(bool interactive READ interactive CONSTANT)
    Q_PROPERTY(bool shadows READ shadows CONSTANT)
    Q_PROPERTY(double orthoScale READ orthoScale CONSTANT)
    Q_PROPERTY(bool transparent READ transparent CONSTANT)
    Q_PROPERTY(int preset READ presetIndex NOTIFY presetChanged)
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY emptyTextChanged)
    Q_PROPERTY(QString failure READ failure NOTIFY stateChanged)
    Q_PROPERTY(QVariantList builtMaterials READ builtMaterials NOTIFY materialsChanged)
public:
    enum class Preset { ThreeQuarter = 0, Top = 1 };
    enum class State { Empty, Loading, Ready, Failed };

    // One FBX of a preview: a floor's storey, the slab above it or its roofs. floorIndex is the floor's place in the plan.
    struct Part {
        QUrl file;
        int floorIndex = 0;
        bool roof = false;
        bool slab = false;
        bool operator==(const Part&) const = default;
    };

    // interactive lets the mouse orbit, pan and zoom. shadows turns on the key light's shadows.
    explicit ModelView(bool interactive, bool shadows, QWidget* parent = nullptr, double orthoScale = 0.0);
    // A view for the plan overlay: flat from above, clear background, at this many pixels per metre. The
    // model's bounds then say which part of the plan is seen.
    ModelView(double orthoScale, QWidget* parent);

    QUrl modelFile() const { return file_; }
    QString modelFileText() const { return file_.toString(); }
    QVector3D boundsMin() const { return boundsMin_; }
    QVector3D boundsMax() const { return boundsMax_; }
    bool interactive() const { return interactive_; }
    // Pixels per metre of the flat top view used for the plan overlay, 0 for the normal orbit camera.
    double orthoScale() const { return orthoScale_; }
    // True when the background is clear instead of the Ground color.
    bool transparent() const { return orthoScale_ > 0; }
    bool shadows() const { return shadows_; }
    int presetIndex() const { return static_cast<int>(preset_); }
    QString emptyText() const { return emptyText_; }
    QString failure() const;

    // Makes the view read build/materials.json from the project folder each time the model loads, and show the
    // building with those materials. Off by default, so asset models keep their own look.
    void useBuiltMaterials(bool on) { useMaterials_ = on; }
    // Shows an asset model with the materials of its info.json (the preview maps), instead of the ones the FBX
    // loader makes. Call before setModel. An empty list leaves the FBX look.
    void setAssetMaterials(const QVariantList& list);
    // The entry of one material as the QML wants it, from absolute image paths (empty when none).
    static QVariantMap materialEntry(const std::filesystem::path& baseColor, const std::filesystem::path& normal,
                                     const std::filesystem::path& roughness, const std::filesystem::path& metallic,
                                     const std::filesystem::path& opacity, const std::filesystem::path& occlusion,
                                     bool blend);
    // One map per built material: key, baseColor, normal, roughness, metallic, opacity, occlusion (urls, empty
    // when none) and blend. Empty when there is no materials.json.
    QVariantList builtMaterials() const { return materials_; }
    int builtMaterialCount() const { return static_cast<int>(materials_.size()); }

    // Loads these FBX files side by side, each by its own loader, all in scene metres. Same bounds rule as
    // setModel. The first part is the modelFile.
    void setParts(const QList<Part>& parts, const QVector3D& boundsMin, const QVector3D& boundsMax);
    // Hides roof parts when showRoofs is off, and when cutFloorIndex is 0 or more every part of a floor above it
    // plus the roofs of that floor and the floors above. Roofs of lower floors stay. New bounds frame what is left. A single model without parts is never hidden.
    void setPartFilter(bool showRoofs, int cutFloorIndex, const QVector3D& boundsMin, const QVector3D& boundsMax);
    QVariantList partUrls() const;
    QVariantList partVisible() const { return partVisible_; }
    int partCount() const { return static_cast<int>(parts_.size()); }
    // Loads this FBX file. The bounds are the box of the model in scene metres (X right, Y up, Z toward the
    // front of the plan), worked out by the caller because the loader reports them wrongly.
    void setModel(const QUrl& file, const QVector3D& boundsMin, const QVector3D& boundsMax);
    // Loads the same file again, after a new build wrote it.
    void reloadModel();
    // Shows nothing and the text instead.
    void clearModel(const QString& emptyText);
    void setPreset(Preset preset);
    Preset preset() const { return preset_; }
    // Frames the whole model again.
    void fit();

    State state() const { return state_; }
    // True when the loaded model has been drawn, or nothing is loaded, or the load failed.
    bool pictureReady() const;
    // The message of a failed load, empty otherwise.
    QString errorText() const { return error_; }

    // Called by the QML scene when the loader changes: 0 empty, 1 loaded, 2 failed.
    Q_INVOKABLE void reportStatus(int status, const QString& error);

signals:
    void modelFileChanged();
    void boundsChanged();
    void presetChanged();
    void emptyTextChanged();
    void stateChanged();
    // The loaded model has been drawn.
    void pictureDrawn();
    void partsChanged();
    void partVisibleChanged();
    void fitRequested();
    void reloadRequested();
    void materialsChanged();

private:
    void startFrameCount();
    void readMaterials();
    void updateVisibility();

    bool interactive_;
    double orthoScale_ = 0.0;
    bool shadows_;
    QUrl file_;
    QList<Part> parts_;
    QVariantList partVisible_;
    bool showRoofs_ = true;
    int cutFloorIndex_ = -1;
    QVector3D boundsMin_;
    QVector3D boundsMax_{1.0f, 1.0f, 1.0f};
    Preset preset_ = Preset::ThreeQuarter;
    QString emptyText_;
    QString error_;
    State state_ = State::Empty;
    int framesSinceLoad_ = 0;
    bool useMaterials_ = false;
    bool assetMaterials_ = false;
    QVariantList materials_;
};

}
