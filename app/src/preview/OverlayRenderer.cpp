#include "OverlayRenderer.h"

#include "GraphicsProbe.h"
#include "ModelView.h"
#include "PreviewView.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace plynth::app {

namespace {

constexpr double kMaxSide = 4096.0;
constexpr int kTimeoutMs = 30000;

}

OverlayRenderer::OverlayRenderer(QObject* parent) : QObject(parent) {}

// Throws away a render that is still running.
OverlayRenderer::~OverlayRenderer()
{
    delete view_.data();
}

// Works out which part of the plan the building covers, makes the hidden flat view of that size and loads the
// model into it.
void OverlayRenderer::render(const QUrl& fbx, const plynth::Plan& plan, double pixelsPerMetre, const QString& floorId)
{
    delete view_.data();
    if (!appGraphics().ok || pixelsPerMetre <= 0.0) {
        emit failed();
        return;
    }
    QVector3D low;
    QVector3D high;
    PreviewView::buildingBounds(plan, &low, &high);

    // The part of the plan, in whole plan pixels, so that the picture lines up with the plan image.
    const double x0 = std::floor(low.x() * pixelsPerMetre);
    const double y0 = std::floor(low.z() * pixelsPerMetre);
    const double x1 = std::ceil(high.x() * pixelsPerMetre);
    const double y1 = std::ceil(high.z() * pixelsPerMetre);
    const double widthPlan = x1 - x0;
    const double heightPlan = y1 - y0;
    const double shrink = std::min(1.0, kMaxSide / std::max(widthPlan, heightPlan));
    sceneRect_ = QRectF(x0, y0, widthPlan, heightPlan);
    const int width = std::max(1, static_cast<int>(std::lround(widthPlan * shrink)));
    const int height = std::max(1, static_cast<int>(std::lround(heightPlan * shrink)));
    const double scale = pixelsPerMetre * shrink;

    auto* view = new ModelView(scale, nullptr);
    view_ = view;
    view->useBuiltMaterials(true);
    view->setAttribute(Qt::WA_DontShowOnScreen);
    view->setFixedSize(width, height);
    connect(view, &ModelView::pictureDrawn, this, [this]() { QTimer::singleShot(0, this, [this]() { finish(true); }); });
    connect(view, &ModelView::stateChanged, this, [this]() {
        if (!view_.isNull() && view_->state() == ModelView::State::Failed)
            finish(false);
    });
    view->show();
    const QVector3D boxLow(static_cast<float>(x0 / pixelsPerMetre), 0.0f, static_cast<float>(y0 / pixelsPerMetre));
    const QVector3D boxHigh(static_cast<float>(x1 / pixelsPerMetre), high.y(), static_cast<float>(y1 / pixelsPerMetre));
    QList<ModelView::Part> chosen;
    if (!floorId.isEmpty() && fbx.isLocalFile()) {
        std::vector<std::string> ids;
        for (const plynth::Floor& floor : plan.floors)
            ids.push_back(floor.id);
        const std::filesystem::path path = fbx.toLocalFile().toStdWString();
        const QList<ModelView::Part> parts = PreviewView::readParts(path, ids);
        const auto it = std::find(ids.begin(), ids.end(), floorId.toStdString());
        if (!parts.isEmpty() && it != ids.end()) {
            for (const ModelView::Part& part : parts)
                if (!part.roof && !part.slab && part.floorIndex == static_cast<int>(it - ids.begin()))
                    chosen.append(part);
            if (chosen.isEmpty()) {
                delete view;
                view_.clear();
                emit failed();
                return;
            }
        }
    }
    if (chosen.isEmpty())
        view->setModel(fbx, boxLow, boxHigh);
    else
        view->setParts(chosen, boxLow, boxHigh);
    QTimer::singleShot(kTimeoutMs, view, [this, view]() {
        if (view_ == view)
            finish(false);
    });
}

// Takes the picture from the hidden view, or reports the failure, and removes the view.
void OverlayRenderer::finish(bool ok)
{
    if (view_.isNull())
        return;
    ModelView* view = view_.data();
    QImage image;
    if (ok)
        image = view->grabFramebuffer();
    view_.clear();
    view->deleteLater();
    if (ok && !image.isNull())
        emit ready(image, sceneRect_);
    else
        emit failed();
}

}
