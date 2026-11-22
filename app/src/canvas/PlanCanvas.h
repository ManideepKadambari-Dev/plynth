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

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
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

// One placed item of the overlay, in scene units. The footprint corners are in itemFootprint order: back
// right, back left, front left, front right. front is the unit direction the item faces.
struct OverlayItem {
    QString id;
    QPolygonF footprint;
    QPointF front;
    QString label;   // the asset name, or "Missing asset"
    QString detail;  // the asset id of a missing asset, drawn in mono
    QString tip;     // the problem sentences, one per line
    bool wall = false;
    bool problem = false;
    bool missing = false;
};

// One problem of the last build, drawn as an amber triangle over its object. position is in scene units.
struct ProblemMarker {
    QPointF position;
    bool error = false;
    QString tip;
};

// One piece of a stair on the plan, in scene units. part is 0 for a step, 1 for a landing and 2 for the
// column of a spiral stair.
struct OverlayStairStep {
    QPolygonF outline;
    int part = 0;
};

// One stair, in scene units: its steps, the outline of the whole footprint, the walk line with its arrow and
// the point where it reaches the floor above. For a stair of the shown floor id is its id; for one arriving from
// below id is its id too and from names the floor it starts on.
struct OverlayStair {
    QString id;
    QString from;
    QVector<OverlayStairStep> steps;
    QPolygonF footprint;
    QPolygonF walkLine;
    QPointF origin;
    QPointF arrival;
    QString tip;     // the problem sentences, one per line
    bool problem = false;
};

// One roof section of the shown floor, in scene units. id is "<floor id>:<index>", the way Selection holds it.
// The arrow is set when the type has a direction: a ridge line for a gable or sawtooth roof (ridge is true) and
// the way a shed falls.
struct OverlayRoof {
    QString id;
    QPainterPath area;
    QPointF labelPoint;
    QString label;
    QString tip;
    bool arrow = false;
    QPointF arrowFrom;
    QPointF arrowTo;
    bool ridge = false;
};

// Everything the canvas draws over the plan image.
struct OverlayData {
    QVector<OverlayItem> items;
    QVector<OverlayWall> walls;
    QVector<OverlayOpening> openings;
    QVector<OverlayPoint> points;
    QVector<OverlayRoom> rooms;
    // Where the plan image sits, in scene units: the floor's image offset times the scale.
    QPointF imageOffset;
    // The walls of the floor below, drawn faint under everything else. Not selectable.
    QPainterPath below;
    // The stairs of the shown floor, and the ones that arrive on it from the floor below (locked, not selectable).
    QVector<OverlayStair> stairs;
    QVector<OverlayStair> arriving;
    // The roof sections of the shown floor, drawn when roof sections are shown.
    QVector<OverlayRoof> roofs;
};

// One proposed wall, in scene units.
struct ProposalWall {
    QPointF from;
    QPointF to;
    double thickness = 0.0;
};

// One proposed opening: its centre, the direction of its wall, the wall thickness and the gap width.
// kind is 0 for a plain opening, 1 for a door and 2 for a window. A door hangs at the end of the wall when
// hingeAtEnd is set, and opens toward the right of the wall's direction when swingRight is set.
struct ProposalOpening {
    QPointF centre;
    QPointF direction;
    double thickness = 0.0;
    double width = 0.0;
    int kind = 0;
    bool hingeAtEnd = false;
    bool swingRight = true;
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
    // The id of the stair of the shown floor whose footprint holds a scene point, or an empty string.
    QString stairAt(const QPointF& scene) const;
    // Shows or hides the roof sections of the shown floor (hatch, label and direction arrow).
    void setRoofSectionsShown(bool shown);
    bool roofSectionsShown() const { return roofsShown_; }
    // The id ("<floor id>:<index>") of the roof section holding a scene point, or an empty string.
    QString roofAt(const QPointF& scene) const;
    // The problems of the last build, and whether they are drawn.
    void setProblems(const QVector<ProblemMarker>& markers);
    void setProblemsShown(bool shown);
    // The top view of the built building, drawn over the plan image in the given place (scene units), and
    // whether it is drawn. An empty image draws nothing.
    void setBuilding3d(const QImage& image, const QRectF& sceneRect);
    void setBuilding3dShown(bool shown);
    // Image alignment: while on, a left drag moves the plan image and the arrow keys nudge it. The canvas only
    // reports the gestures; the tools turn them into commands.
    void setAlignMode(bool on);
    bool alignMode() const { return alignMode_; }
    // Where the plan image sits, in scene units.
    QPointF imageOffset() const { return imageOffset_; }
    void setImageOffset(const QPointF& offset);
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
    // An asset tile was dropped on the plan. scene is where, in image pixels. Ctrl was held when it dropped.
    void assetDropped(const QString& assetId, const QPointF& scene, bool free);
    // Image alignment gestures. A drag sends alignDragged with the distance from where it started, in scene
    // units, then alignDragFinished. A nudge says which way, and whether Shift made it a big one.
    void alignDragStarted();
    void alignDragged(const QPointF& delta);
    void alignDragFinished();
    void alignNudged(const QPoint& direction, bool big);
    // The Select tool was pressed on a roof section while roof sections are shown. id is "<floor id>:<index>".
    void roofSectionClicked(const QString& id);

protected:
    bool event(QEvent* event) override;
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
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setZoomAt(double zoom, const QPoint& viewportAnchor);
    void updateSceneRect();
    void updateCursor();
    void paintOverlay(QPainter* painter);
    // Where a room's name and area chip is drawn, in canvas pixels.
    struct RoomChip {
        const OverlayRoom* room = nullptr;
        QRectF pill;
        QRectF nameBox;
        QRectF areaBox;
    };
    QVector<RoomChip> roomChips() const;
    void paintLabels(QPainter* painter);
    void paintItems(QPainter* painter);
    bool overRoofCover(const QPointF& scene) const;
    void paintRoofHatch(QPainter* painter);
    void paintRoofLabels(QPainter* painter);
    void paintProposals(QPainter* painter);
    void paintProblems(QPainter* painter);
    void applyFit();
    QRectF imageRect() const;

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QString emptyText_;
    bool dimmed_ = false;
    OverlayData overlay_;
    ProposalOverlay proposals_;
    QVector<ProblemMarker> problems_;
    bool problemsShown_ = true;
    bool roofsShown_ = false;
    QImage building3d_;
    QRectF building3dRect_;
    bool building3dShown_ = false;
    SelectionKind selectionKind_ = SelectionKind::None;
    QString selectionId_;
    double gridStep_ = 0.0;
    QPointF imageOffset_;
    bool alignMode_ = false;
    bool aligning_ = false;
    QPointF alignStart_;
    Tool* tool_ = nullptr;
    double zoom_ = 1.0;
    bool fitMode_ = true;
    bool spaceHeld_ = false;
    bool panning_ = false;
    QPoint lastPanPos_;
    Qt::MouseButton panButton_ = Qt::NoButton;
};

}
