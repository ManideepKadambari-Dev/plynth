#include "RenderHost.h"

#include "GraphicsProbe.h"
#include "ModelView.h"
#include "PreviewView.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace plynth::app {

namespace {

constexpr int kPollMs = 50;
constexpr int kColourTolerance = 12;

// Swallows a Qt message, so nothing but the answer reaches the console.
void dropMessage(QtMsgType, const QMessageLogContext&, const QString&)
{
}

// Starts the application a render needs when the program runs as a command. Pixels are not scaled for the
// screen, so the picture has exactly the asked size.
std::unique_ptr<QApplication> startApplication()
{
    if (QCoreApplication::instance() != nullptr)
        return nullptr;
    qInstallMessageHandler(dropMessage);
    if (qEnvironmentVariableIsEmpty("QT_ENABLE_HIGHDPI_SCALING"))
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    static int argc = 1;
    static char name[] = "plynth";
    static char* argv[] = {name, nullptr};
    auto app = std::make_unique<QApplication>(argc, argv);
    // The same colors and lights as the 3D page, which reads them from the theme file the app loads at start.
    Theme::instance().load(QStringLiteral(":/themes/drafting-dark.json"));
    return app;
}

// Turns Qt text into core text.
std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

// Says whether a picture shows anything but the background: a few pixels must differ from the Ground color.
bool showsSomething(const QImage& picture, const QColor& ground)
{
    const QImage image = picture.convertToFormat(QImage::Format_RGB32);
    const long long needed = std::max<long long>(16, static_cast<long long>(image.width()) * image.height() / 1000);
    long long differing = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = row[x];
            const int d = std::max({std::abs(qRed(p) - ground.red()), std::abs(qGreen(p) - ground.green()),
                                    std::abs(qBlue(p) - ground.blue())});
            if (d > kColourTolerance && ++differing >= needed)
                return true;
        }
    }
    return false;
}

}

// Loads the build into a hidden view of the shot's size, waits until it has been drawn and saves the picture.
plynth::Status renderShot(const plynth::RenderShot& shot, std::stop_token stop)
{
    const std::unique_ptr<QApplication> ownApp = startApplication();
    const plynth::GraphicsProbe& graphics = appGraphics();
    if (!graphics.ok)
        return plynth::fail(plynth::ErrorCode::GraphicsUnavailable,
                            graphics.problem.empty() ? std::string("The 3D view cannot start.") : graphics.problem);

    const plynth::RenderPlan& plan = shot.plan;
    const int width = plan.options.width;
    const int height = plan.options.height;
    auto view = std::make_unique<ModelView>(false, true);
    if (view->status() == QQuickWidget::Error)
        return plynth::fail(plynth::ErrorCode::GraphicsUnavailable, toCore(view->errorText()));
    view->useBuiltMaterials(true);
    view->setAttribute(Qt::WA_DontShowOnScreen);
    view->setFixedSize(width, height);
    view->show();

    std::vector<std::string> floorIds;
    for (const plynth::Floor& floor : plan.built.floors)
        floorIds.push_back(floor.id);
    QVector3D low;
    QVector3D high;
    PreviewView::buildingBounds(plan.built, &low, &high);
    QVector3D frameHigh = high;
    if (plan.cutFloorIndex >= 0) {
        QVector3D cutLow;
        QVector3D cutHigh;
        PreviewView::buildingBounds(plan.built, &cutLow, &cutHigh, plan.cutFloorIndex, false);
        frameHigh = QVector3D(high.x(), cutHigh.y(), high.z());
    }
    QList<ModelView::Part> parts = PreviewView::readParts(plan.fbx, floorIds);
    if (parts.isEmpty())
        parts.append(ModelView::Part{QUrl::fromLocalFile(QString::fromStdWString(plan.fbx.wstring())), 0, false});

    view->setAngle(plan.options.yaw.value_or(45.0), plan.options.pitch.value_or(25.0));
    view->setParts(parts, low, high);
    view->setPartFilter(plan.options.showRoofs, plan.cutFloorIndex, low, frameHigh);

    enum class End { Waiting, Drawn, Failed, TimedOut, Stopped };
    End end = End::Waiting;
    QEventLoop loop;
    const auto finish = [&](End how) {
        if (end != End::Waiting)
            return;
        end = how;
        loop.quit();
    };
    QObject::connect(view.get(), &ModelView::pictureDrawn, &loop,
                     [&]() { QTimer::singleShot(0, &loop, [&]() { finish(End::Drawn); }); });
    QObject::connect(view.get(), &ModelView::stateChanged, &loop, [&]() {
        if (view->state() == ModelView::State::Failed)
            finish(End::Failed);
    });
    QElapsedTimer clock;
    clock.start();
    const qint64 limitMs = std::chrono::duration_cast<std::chrono::milliseconds>(shot.timeout).count();
    QTimer poll;
    poll.setInterval(kPollMs);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (stop.stop_requested())
            finish(End::Stopped);
        else if (clock.elapsed() > limitMs)
            finish(End::TimedOut);
    });
    poll.start();
    if (view->state() == ModelView::State::Failed)
        end = End::Failed;
    else
        loop.exec();
    poll.stop();

    plynth::Status result;
    if (end == End::Stopped) {
        result = plynth::fail(plynth::ErrorCode::Cancelled, "The render was stopped.");
    } else if (end == End::TimedOut) {
        result = plynth::fail(plynth::ErrorCode::RenderFailed, "The 3D view did not finish the picture within " +
                                                                   std::to_string(shot.timeout.count()) + " seconds.");
    } else if (end == End::Failed) {
        result = plynth::fail(plynth::ErrorCode::RenderFailed, toCore(view->errorText()));
    } else {
        QImage image = view->grabFramebuffer();
        if (!image.isNull() && image.size() != QSize(width, height))
            image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (image.isNull()) {
            result = plynth::fail(plynth::ErrorCode::RenderFailed, "The 3D view gave no picture.");
        } else if (!showsSomething(image, Theme::instance().ground())) {
            result = plynth::fail(plynth::ErrorCode::RenderFailed,
                                  "The 3D view drew only the background. The building may be empty or the graphics "
                                  "driver may have failed.");
        } else {
            QImage flat = image.convertToFormat(QImage::Format_RGB32);
            flat.setDevicePixelRatio(1.0);
            if (!flat.save(QString::fromStdWString(shot.temporaryPng.wstring()), "PNG"))
                result = plynth::fail(plynth::ErrorCode::CannotWrite, "The picture could not be written.");
        }
    }
    view.reset();
    return result;
}

}
