#pragma once

#include "tools/Tool.h"

#include <optional>

namespace plynth::app {

// Measures the scale from two clicks on the plan image and a typed real distance.
class ScaleTool : public Tool {
public:
    explicit ScaleTool(ToolController& host) : Tool(host) {}

    ToolId id() const override { return ToolId::Scale; }
    QString hint() const override;

    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseLeft() override;
    bool keyPressed(QKeyEvent* event) override;
    void modifiersChanged() override;
    void paint(QPainter& painter) override;

private:
    QPointF endFor(const ToolMouse& mouse) const;
    void finish(QPointF second);

    std::optional<QPointF> first_;
    std::optional<ToolMouse> last_;
};

}
