#pragma once

#include "tools/Tool.h"

#include <plynth/core/Plan.h>

#include <optional>

namespace plynth::app {

// Picks points, walls and rooms by clicking, and moves points by dragging.
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
    bool keyPressed(QKeyEvent* event) override;

private:
    void dragTo(const ToolMouse& mouse);

    plynth::Id dragPoint_;
    QPoint pressView_;
    QPointF startMetres_;
    int dragKey_ = 0;
    bool dragging_ = false;
};

}
