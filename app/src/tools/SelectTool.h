#pragma once

#include "tools/Tool.h"

#include <plynth/core/Plan.h>

#include <QPointF>

#include <optional>

namespace plynth::app {

// Picks points, walls, openings and rooms by clicking. Drags points, drags the bend handle of the selected
// wall, and drags openings along their wall.
class SelectTool : public Tool {
public:
    explicit SelectTool(ToolController& host) : Tool(host) {}

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
    void bendTo(const ToolMouse& mouse);
    std::optional<QPointF> bendHandleView() const;
    bool overBendHandle(const QPoint& view) const;
    void setHandleHover(bool hover);

    plynth::Id dragPoint_;
    QPoint pressView_;
    QPointF startMetres_;
    int dragKey_ = 0;
    bool dragging_ = false;

    plynth::Id bendWall_;
    bool handleHover_ = false;
};

}
