#pragma once

#include "tools/Tool.h"

namespace plynth::app {

// Finds walls, rooms and openings in the plan image and shows them as a preview. The settings are in the
// properties panel, Enter accepts. The tool itself takes no clicks.
class DetectTool : public Tool {
public:
    explicit DetectTool(ToolController& host) : Tool(host) {}

    ToolId id() const override { return ToolId::Detect; }
    QString hint() const override;
    Qt::CursorShape cursor() const override { return Qt::ArrowCursor; }

    void activate() override;
    void deactivate() override;
    bool keyPressed(QKeyEvent* event) override;
    void documentChanged(unsigned changes) override;
};

}
