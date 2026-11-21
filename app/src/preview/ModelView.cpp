#include "ModelView.h"

#include "ThemeQml.h"
#include "theme/Theme.h"

#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QTimer>

namespace plynth::app {

namespace {

constexpr int kFramesAfterLoad = 3;

// The one theme object every scene shares.
ThemeQml* sharedTheme()
{
    static ThemeQml* theme = new ThemeQml(nullptr);
    return theme;
}

}

// Makes the scene, gives it the theme and itself as "scene", and starts the QML.
ModelView::ModelView(bool interactive, bool shadows, QWidget* parent)
    : QQuickWidget(parent), interactive_(interactive), shadows_(shadows)
{
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(Theme::instance().ground());
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
