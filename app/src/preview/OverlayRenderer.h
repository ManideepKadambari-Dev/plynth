#pragma once

#include <plynth/core/Plan.h>

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QUrl>

namespace plynth::app {

class ModelView;

// Draws the built building once, flat from above, into an image that lines up with the plan image: world
// (X, Y) lands on plan pixel (X, Y) times pixels per metre. It runs the same 3D scene as the 3D page in a
// hidden widget and never changes the plan.
class OverlayRenderer : public QObject {
    Q_OBJECT
public:
    explicit OverlayRenderer(QObject* parent = nullptr);
    ~OverlayRenderer() override;

    // Starts drawing. The result comes back as ready or failed. A call while one runs replaces it.
    void render(const QUrl& fbx, const plynth::Plan& plan, double pixelsPerMetre);
    bool busy() const { return !view_.isNull(); }

signals:
    // The picture and the place it covers, in plan image pixels.
    void ready(const QImage& image, const QRectF& sceneRect);
    void failed();

private:
    void finish(bool ok);

    QPointer<ModelView> view_;
    QRectF sceneRect_;
};

}
