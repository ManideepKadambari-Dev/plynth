#pragma once

#include "tools/CanopyTool.h"
#include "tools/LightTool.h"
#include "tools/StairTool.h"
#include "tools/Tool.h"

#include <plynth/core/Plan.h>
#include <plynth/core/Snapping.h>

#include <QPointF>

#include <optional>

namespace plynth::app {

// Picks points, walls, openings, items, rooms, light markers and canopies by clicking. Drags light markers and the
// corners of the selected canopy too. Drags points, items and openings, drags the bend handle of the selected wall,
// and turns the selected item with its rotation handle.
class SelectTool : public Tool {
public:
    explicit SelectTool(ToolController& host) : Tool(host), stairs_(host), lights_(host), canopies_(host) {}

    ToolId id() const override { return ToolId::Select; }
    QString hint() const override;
    Qt::CursorShape cursor() const override { return Qt::ArrowCursor; }

    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseReleased(const ToolMouse& mouse) override;
    void mouseLeft() override;
    bool keyPressed(QKeyEvent* event) override;
    void paint(QPainter& painter) override;

private:
    void dragTo(const ToolMouse& mouse);
    void showWallMenu(const ToolMouse& mouse);
    void bendTo(const ToolMouse& mouse);
    void moveOpeningTo(const ToolMouse& mouse);
    void moveItemTo(const ToolMouse& mouse);
    void rotateItemTo(const ToolMouse& mouse);
    std::optional<QPointF> rotateHandleView() const;
    bool overRotateHandle(const QPoint& view) const;
    std::optional<QPointF> bendHandleView() const;
    bool overBendHandle(const QPoint& view) const;
    void setHandleHover(bool hover);

    plynth::Id dragPoint_;
    std::optional<plynth::SnapResult> dragSnap_;
    QPoint pressView_;
    QPointF startMetres_;
    int dragKey_ = 0;
    bool dragging_ = false;

    plynth::Id dragOpening_;
    double openingGrab_ = 0.0;
    plynth::Id bendWall_;
    plynth::Id dragItem_;
    plynth::Id rotateItem_;
    QPointF itemGrab_;
    bool handleHover_ = false;
    StairEditor stairs_;
    LightEditor lights_;
    CanopyEditor canopies_;
};

}
