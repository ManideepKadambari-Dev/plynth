#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector>

#include "document/Selection.h"

class QGraphicsPixmapItem;
class QGraphicsScene;

namespace plynth::app {

class Tool;

// One wall of the overlay, in scene units (image pixels).
struct OverlayWall {
    QString id;
    QPointF from;
    QPointF to;
    double thickness = 0.0;
};

// One point of the overlay, in scene units.
struct OverlayPoint {
    QString id;
    QPointF position;
};

// One room of the overlay. floor is the floor area with its holes, label is where the name goes.
struct OverlayRoom {
    QString id;
    QPainterPath floor;
    QPointF label;
    QString name;
    QString area;
};

// Everything the canvas draws over the plan image.
struct OverlayData {
    QVector<OverlayWall> walls;
    QVector<OverlayPoint> points;
    QVector<OverlayRoom> rooms;
};

// The plan sheet: shows one plan image and lets the user pan and zoom it.
// In scene coordinates one unit is one image pixel, with the image corner at (0, 0).
class PlanCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit PlanCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    bool hasImage() const { return pixmapItem_ != nullptr; }
    void setEmptyText(const QString& text);
    static QString defaultEmptyText();
    void setDimmed(bool dimmed);
    void setOverlay(const OverlayData& overlay);
    void setSelection(SelectionKind kind, const QString& id);
    // Grid step in scene units, or 0 for no grid. The grid shows only when a step is wide enough on screen.
    void setGridStep(double scenePixels);
    // The tool that gets the mouse and keys and draws its previews.
    void setTool(Tool* tool);
    double zoom() const { return zoom_; }
    QPointF sceneToView(const QPointF& scene) const;

    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void actualSize();

signals:
    void zoomChanged(double zoom);
    void cursorMoved(const QPointF& scene);
    void cursorLeft();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setZoomAt(double zoom, const QPoint& viewportAnchor);
    void updateSceneRect();
    void updateCursor();
    void paintOverlay(QPainter* painter);
    void paintLabels(QPainter* painter);
    void applyFit();
    QRectF imageRect() const;

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QString emptyText_;
    bool dimmed_ = false;
    OverlayData overlay_;
    SelectionKind selectionKind_ = SelectionKind::None;
    QString selectionId_;
    double gridStep_ = 0.0;
    Tool* tool_ = nullptr;
    double zoom_ = 1.0;
    bool fitMode_ = true;
    bool spaceHeld_ = false;
    bool panning_ = false;
    QPoint lastPanPos_;
    Qt::MouseButton panButton_ = Qt::NoButton;
};

}
