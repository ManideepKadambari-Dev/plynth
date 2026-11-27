#pragma once

#include "tools/Tool.h"

#include <plynth/core/Geometry.h>
#include <plynth/core/Plan.h>
#include <plynth/core/Snapping.h>

#include <QPoint>
#include <QPointF>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace plynth::app {

// Selects canopies and drags the corners of the selected one. The Canopy tool and the Select tool both use it.
// A corner drag is SetCanopyCommand runs with one merge key, so one drag is one undo step.
class CanopyEditor {
public:
    explicit CanopyEditor(ToolController& host) : host_(host) {}

    // True while a corner is dragged.
    bool busy() const { return corner_ >= 0; }
    // A press on a corner handle of the selected canopy starts a drag. Returns true when it did.
    bool pressHandle(const ToolMouse& mouse);
    // A press on a canopy selects it. Returns true when it did.
    bool pressBody(const ToolMouse& mouse);
    void moved(const ToolMouse& mouse);
    void released();
    // True when the cursor is over a corner handle of the selected canopy.
    bool overHandle(const QPoint& view) const;
    void paintHandles(QPainter& painter, bool hover) const;
    void reset();

private:
    const plynth::Canopy* selected() const;
    int handleAt(const QPoint& view) const;

    ToolController& host_;
    plynth::Id canopy_;
    int corner_ = -1;
    int key_ = 0;
    std::optional<plynth::SnapResult> snap_;
};

// Draws a canopy: click its corners on the plan, then click the first corner again or press Enter. A small menu
// lists the roof types of the registry, and picking one adds the canopy. A click on a canopy selects it, and the
// corners of the selected one can be dragged. Backspace takes back the last corner, Esc stops.
class CanopyTool : public Tool {
public:
    explicit CanopyTool(ToolController& host) : Tool(host), editor_(host) {}

    ToolId id() const override { return ToolId::Canopy; }
    QString hint() const override;

    void activate() override;
    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseReleased(const ToolMouse& mouse) override;
    void mouseLeft() override;
    bool keyPressed(QKeyEvent* event) override;
    void modifiersChanged() override;
    void floorChanged() override;
    void documentChanged(unsigned changes) override;
    void paint(QPainter& painter) override;

    // Shows an outline being drawn through places in metres, the last one as the cursor. For checks.
    void previewOutline(const std::vector<plynth::Vec2>& corners);
    // Adds a canopy over an outline with a roof type and selects it. False when the command is refused.
    bool placeOutline(const std::vector<plynth::Vec2>& outline, const std::string& roofType);

private:
    plynth::SnapResult snapFor(QPointF scene, Qt::KeyboardModifiers modifiers) const;
    bool nearFirstCorner(const QPoint& view) const;
    void finish(const QPoint& global);
    void cancel();
    void refreshPreview();

    CanopyEditor editor_;
    std::vector<plynth::Vec2> corners_;
    std::optional<QPointF> lastScene_;
    std::optional<plynth::SnapResult> preview_;
    bool menuOpen_ = false;
};

}
