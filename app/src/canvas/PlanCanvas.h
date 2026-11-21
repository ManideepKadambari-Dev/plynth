#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

#include "document/Selection.h"

class QGraphicsPixmapItem;
class QGraphicsScene;

namespace plynth::app {

class Tool;

// One wall of the overlay: its band as drawn, with curves and rounded corners, in scene units (image pixels).
struct OverlayWall {
    QString id;
    QPainterPath band;
};

// One opening of the overlay, in scene units. The footprint is the cut through the wall band. A door has
// a leaf and the arc it sweeps, a window has two thin lines along the wall. type is 0 plain, 1 door, 2 window.
struct OverlayOpening {
    QString id;
    int type = 0;
    QPolygonF footprint;
    QLineF startJamb;
    QLineF endJamb;
    QLineF leaf;
    QPolygonF arc;
    QPolygonF glassA;
    QPolygonF glassB;
    bool problem = false;
    bool valid = false;
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
    QVector<OverlayOpening> openings;
    QVector<OverlayPoint> points;
    QVector<OverlayRoom> rooms;
};

// One proposed wall, in scene units.
struct ProposalWall {
    QPointF from;
    QPointF to;
    double thickness = 0.0;
};

// One proposed opening: its centre, the direction of its wall, the wall thickness and the gap width.
// kind is 0 for a plain opening, 1 for a door and 2 for a window.
struct ProposalOpening {
    QPointF centre;
    QPointF direction;
    double thickness = 0.0;
    double width = 0.0;
    int kind = 0;
};

// Everything the canvas draws for detection proposals: only the new ones, as a dashed blue preview.
struct ProposalOverlay {
    QVector<ProposalWall> walls;
    QVector<QPointF> points;
    QVector<QVector<QPolygonF>> rooms;
    QVector<ProposalOpening> openings;

    bool isEmpty() const { return walls.isEmpty() && rooms.isEmpty() && openings.isEmpty(); }
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
    void setProposals(const ProposalOverlay& proposals);
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
    void paintProposals(QPainter* painter);
    void applyFit();
    QRectF imageRect() const;

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QString emptyText_;
    bool dimmed_ = false;
    OverlayData overlay_;
    ProposalOverlay proposals_;
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
