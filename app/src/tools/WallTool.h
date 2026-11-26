#pragma once

#include "tools/Tool.h"

#include <plynth/core/Snapping.h>
#include <plynth/core/commands/WallCommands.h>

#include <optional>

namespace plynth::app {

// Draws walls by clicking points. Each click after the first runs one DrawWallCommand and carries on from
// the point it made, until the loop is closed or the chain is ended.
class WallTool : public Tool {
public:
    explicit WallTool(ToolController& host) : Tool(host) {}

    ToolId id() const override { return ToolId::Wall; }
    QString hint() const override;

    void activate() override;
    void deactivate() override;
    void floorChanged() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseLeft() override;
    bool keyPressed(QKeyEvent* event) override;
    void modifiersChanged() override;
    void documentChanged(unsigned changes) override;
    void paint(QPainter& painter) override;

private:
    plynth::SnapResult snapFor(QPointF scene, Qt::KeyboardModifiers modifiers) const;
    double previewThickness() const;
    void endChain();
    void refreshPreview();

    bool chaining_ = false;
    plynth::WallEnd start_;
    plynth::Vec2 startAt_;
    plynth::Id chainFirstPoint_;
    plynth::Id chainFloor_;
    bool running_ = false;
    std::optional<QPointF> lastScene_;
    std::optional<plynth::SnapResult> preview_;
};

}
