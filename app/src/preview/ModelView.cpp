#include "ModelView.h"

#include "ThemeQml.h"
#include "theme/Theme.h"

#include <plynth/core/Build.h>

#include <QDebug>
#include <QImageReader>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

namespace plynth::app {

namespace {

constexpr int kFramesAfterLoad = 3;

// In a Debug build, prints once how large each texture file is, to check that none is over the preview size.
void logTextureSizes(const QVariantList& list)
{
#ifndef NDEBUG
    static QSet<QString> seen;
    for (const QVariant& item : list) {
        const QVariantMap entry = item.toMap();
        for (const char* name : {"baseColor", "normal", "roughness", "metallic", "opacity", "occlusion"}) {
            const QString file = QUrl(entry.value(QLatin1String(name)).toString()).toLocalFile();
            if (file.isEmpty() || seen.contains(file))
                continue;
            seen.insert(file);
            const QSize size = QImageReader(file).size();
            qDebug().noquote() << "preview texture" << size.width() << "x" << size.height() << file;
        }
    }
#else
    Q_UNUSED(list)
#endif
}

// The one theme object every scene shares.
ThemeQml* sharedTheme()
{
    static ThemeQml* theme = new ThemeQml(nullptr);
    return theme;
}

}

// A view for the plan overlay.
ModelView::ModelView(double orthoScale, QWidget* parent) : ModelView(false, false, parent, orthoScale)
{
}

// Makes the scene, gives it the theme and itself as "scene", and starts the QML.
ModelView::ModelView(bool interactive, bool shadows, QWidget* parent, double orthoScale)
    : QQuickWidget(parent), interactive_(interactive), orthoScale_(orthoScale), shadows_(shadows)
{
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    if (orthoScale > 0) {
        setAttribute(Qt::WA_TranslucentBackground);
        setClearColor(Qt::transparent);
    } else {
        setClearColor(Theme::instance().ground());
    }
    rootContext()->setContextProperty(QStringLiteral("Theme"), sharedTheme());
    rootContext()->setContextProperty(QStringLiteral("scene"), this);
    connect(quickWindow(), &QQuickWindow::afterRendering, this, [this]() {
        if (state_ == State::Ready && framesSinceLoad_ < kFramesAfterLoad && ++framesSinceLoad_ == kFramesAfterLoad)
            emit pictureDrawn();
    });
    setSource(QUrl(QStringLiteral("qrc:/qml/BuildScene.qml")));
    if (status() == QQuickWidget::Error) {
        state_ = State::Failed;
        error_ = tr("The 3D view could not start.");
        const QList<QQmlError> problems = errors();
        if (!problems.isEmpty())
            error_ += QLatin1Char(' ') + problems.first().description();
    }
}

// Makes one entry of the material list from absolute image paths.
QVariantMap ModelView::materialEntry(const std::filesystem::path& baseColor, const std::filesystem::path& normal,
                                     const std::filesystem::path& roughness, const std::filesystem::path& metallic,
                                     const std::filesystem::path& opacity, const std::filesystem::path& occlusion,
                                     bool blend)
{
    auto url = [](const std::filesystem::path& path) {
        return path.empty() ? QString() : QUrl::fromLocalFile(QString::fromStdWString(path.wstring())).toString();
    };
    QString key = QString::fromStdWString(baseColor.wstring());
    key.replace(QLatin1Char('\\'),QLatin1Char('/'));
    QVariantMap entry;
    entry.insert(QStringLiteral("key"), key.toLower());
    entry.insert(QStringLiteral("baseColor"), url(baseColor));
    entry.insert(QStringLiteral("normal"), url(normal));
    entry.insert(QStringLiteral("roughness"), url(roughness));
    entry.insert(QStringLiteral("metallic"), url(metallic));
    entry.insert(QStringLiteral("opacity"), url(opacity));
    entry.insert(QStringLiteral("occlusion"), url(occlusion));
    entry.insert(QStringLiteral("blend"), blend);
    return entry;
}

// Keeps the materials of an asset for the next model load.
void ModelView::setAssetMaterials(const QVariantList& list)
{
    assetMaterials_ = true;
    materials_ = list;
    logTextureSizes(materials_);
    emit materialsChanged();
}

// Reads materials.json of the project that holds the loaded building.fbx, which sits in the build folder. A
// missing or broken file leaves the list empty, so the FBX keeps its own materials. Furniture entries are
// included, so furniture shows its maps too.
void ModelView::readMaterials()
{
    if (assetMaterials_)
        return;
    QVariantList list;
    if (useMaterials_ && file_.isLocalFile()) {
        const std::filesystem::path fbx = file_.toLocalFile().toStdWString();
        const plynth::BuiltMaterialsRead read = plynth::readBuiltMaterials(fbx.parent_path().parent_path());
        if (read.status.ok()) {
            for (const plynth::BuiltMaterial& m : read.materials)
                list.append(materialEntry(m.baseColor, m.normal, m.roughness, m.metallic, m.opacity, m.occlusion,
                                          m.alphaMode == plynth::AlphaMode::Blend));
        }
    }
    materials_ = list;
    logTextureSizes(materials_);
    emit materialsChanged();
}

// The text the QML shows in amber: the message of a failed load.
QString ModelView::failure() const
{
    return state_ == State::Failed ? error_ : QString();
}

// Starts loading a file and tells the scene how big the model is.
void ModelView::setModel(const QUrl& file, const QVector3D& boundsMin, const QVector3D& boundsMax)
{
    if (state_ == State::Failed && status() == QQuickWidget::Error)
        return;
    boundsMin_ = boundsMin;
    boundsMax_ = boundsMax;
    const bool same = file == file_;
    file_ = file;
    readMaterials();
    state_ = State::Loading;
    error_.clear();
    framesSinceLoad_ = 0;
    emit stateChanged();
    emit boundsChanged();
    if (same)
        emit reloadRequested();
    else
        emit modelFileChanged();
}

// Loads the current file again.
void ModelView::reloadModel()
{
    if (file_.isEmpty())
        return;
    readMaterials();
    state_ = State::Loading;
    framesSinceLoad_ = 0;
    emit stateChanged();
    emit reloadRequested();
}

// Drops the model and shows the text.
void ModelView::clearModel(const QString& emptyText)
{
    if (status() == QQuickWidget::Error)
        return;
    file_.clear();
    state_ = State::Empty;
    error_.clear();
    emptyText_ = emptyText;
    emit modelFileChanged();
    emit emptyTextChanged();
    emit stateChanged();
}

// Switches to a named view and frames the model.
void ModelView::setPreset(Preset preset)
{
    preset_ = preset;
    emit presetChanged();
}

// Asks the scene to frame the model again.
void ModelView::fit()
{
    emit fitRequested();
}

// Ready once the model has been drawn a few times, or when there is nothing to wait for.
bool ModelView::pictureReady() const
{
    if (state_ == State::Loading)
        return false;
    if (state_ == State::Ready)
        return framesSinceLoad_ >= kFramesAfterLoad;
    return true;
}

// Takes the loader's state from the QML and asks for frames so a loaded model is drawn.
void ModelView::reportStatus(int status, const QString& error)
{
    if (status == 1) {
        if (state_ == State::Ready)
            return;
        state_ = State::Ready;
        error_.clear();
        startFrameCount();
    } else if (status == 2) {
        state_ = State::Failed;
        error_ = tr("The 3D model could not be loaded. %1").arg(error);
    } else if (!file_.isEmpty()) {
        state_ = State::Loading;
    } else {
        state_ = State::Empty;
    }
    emit stateChanged();
}

// Keeps asking the window to redraw until enough frames have passed after the load.
void ModelView::startFrameCount()
{
    framesSinceLoad_ = 0;
    auto* timer = new QTimer(this);
    timer->setInterval(30);
    connect(timer, &QTimer::timeout, this, [this, timer]() {
        quickWindow()->update();
        if (state_ != State::Ready || framesSinceLoad_ >= kFramesAfterLoad) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

}
