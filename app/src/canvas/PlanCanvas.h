#pragma once

#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;
class QGraphicsScene;

namespace plynth::app {

// The plan sheet: shows one plan image and lets the user pan and zoom it.
// In scene coordinates one unit is one image pixel, with the image corner at (0, 0).
class PlanCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit PlanCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    bool hasImage() const { return pixmapItem_ != nullptr; }
    double zoom() const { return zoom_; }

    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void actualSize();

signals:
    void zoomChanged(double zoom);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setZoomAt(double zoom, const QPoint& viewportAnchor);
    void updateSceneRect();
    void updateCursor();
    void applyFit();
    QRectF imageRect() const;

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    double zoom_ = 1.0;
    bool fitMode_ = true;
    bool spaceHeld_ = false;
    bool panning_ = false;
    QPoint lastPanPos_;
    Qt::MouseButton panButton_ = Qt::NoButton;
};

}
