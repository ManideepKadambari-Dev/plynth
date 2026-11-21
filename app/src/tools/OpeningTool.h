#pragma once

#include "canvas/PlanCanvas.h"
#include "tools/Tool.h"

#include <plynth/core/Plan.h>
#include <plynth/core/Status.h>

#include <QPointF>

#include <optional>

namespace plynth::app {

// Places doors, windows and plain openings on walls. Hover shows where the opening would go, snapped along the
// wall in 5 cm steps; a click adds it. The sizes come from the project defaults.
class OpeningTool : public Tool {
public:
    explicit OpeningTool(ToolController& host) : Tool(host) {}

    ToolId id() const override { return ToolId::Opening; }
    QString hint() const override;

    void activate() override;
    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseLeft() override;
    void modifiersChanged() override;
    void documentChanged(unsigned changes) override;
    void paint(QPainter& painter) override;

private:
    struct Preview {
        plynth::Opening opening;
        plynth::Status fit;
        OverlayOpening shape;
    };

    void refreshPreview();

    std::optional<QPointF> lastScene_;
    std::optional<Preview> preview_;
    QString lastMessage_;
};

}
