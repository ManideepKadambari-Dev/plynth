#pragma once

#include <QQuickWidget>
#include <QString>
#include <QUrl>
#include <QVector3D>

namespace plynth::app {

class ThemeQml;

// One 3D scene in a widget: a model loaded from an FBX file, lit, with a camera that orbits it. It is the
// QML scene of app/qml/BuildScene.qml and only shows things, it never changes the plan.
class ModelView : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QString modelFile READ modelFileText NOTIFY modelFileChanged)
    Q_PROPERTY(QVector3D boundsMin READ boundsMin NOTIFY boundsChanged)
    Q_PROPERTY(QVector3D boundsMax READ boundsMax NOTIFY boundsChanged)
    Q_PROPERTY(bool interactive READ interactive CONSTANT)
    Q_PROPERTY(bool shadows READ shadows CONSTANT)
    Q_PROPERTY(int preset READ presetIndex NOTIFY presetChanged)
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY emptyTextChanged)
    Q_PROPERTY(QString failure READ failure NOTIFY stateChanged)
public:
    enum class Preset { ThreeQuarter = 0, Top = 1 };
    enum class State { Empty, Loading, Ready, Failed };

    // interactive lets the mouse orbit, pan and zoom. shadows turns on the key light's shadows.
    explicit ModelView(bool interactive, bool shadows, QWidget* parent = nullptr);

    QUrl modelFile() const { return file_; }
    QString modelFileText() const { return file_.toString(); }
    QVector3D boundsMin() const { return boundsMin_; }
    QVector3D boundsMax() const { return boundsMax_; }
    bool interactive() const { return interactive_; }
    bool shadows() const { return shadows_; }
    int presetIndex() const { return static_cast<int>(preset_); }
    QString emptyText() const { return emptyText_; }
    QString failure() const;

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
    void fitRequested();
    void reloadRequested();

private:
    void startFrameCount();

    bool interactive_;
    bool shadows_;
    QUrl file_;
    QVector3D boundsMin_;
    QVector3D boundsMax_{1.0f, 1.0f, 1.0f};
    Preset preset_ = Preset::ThreeQuarter;
    QString emptyText_;
    QString error_;
    State state_ = State::Empty;
    int framesSinceLoad_ = 0;
};

}
