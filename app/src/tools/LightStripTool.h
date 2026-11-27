#pragma once

#include "tools/Tool.h"

#include <plynth/core/FinishSettings.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/Plan.h>

#include <QPoint>
#include <QPointF>
#include <QString>

#include <optional>
#include <vector>

namespace plynth::app {

// The plain name of a light strip kind, such as "Cove strip".
QString lightStripKindText(plynth::LightStripKind kind);
// One sentence on where a strip of a kind goes and which way it shines, for tooltips.
QString lightStripKindTip(plynth::LightStripKind kind);

// Selects light strips of the shown floor and drags their points. The Light strip tool and the Select tool both
// use it. A press on a point handle of the selected strip moves that point; a press on a strip's line selects it
// and a drag moves the whole line. Each drag is SetLightStripCommand runs with one merge key, so it is one undo
// step. Step lights follow their stair and only get selected.
class LightStripEditor {
public:
    explicit LightStripEditor(ToolController& host) : host_(host) {}

    // True while a strip is pressed or dragged.
    bool busy() const { return mode_ != Mode::None; }
    // Returns true when the press was on a strip.
    bool pressBody(const ToolMouse& mouse);
    void moved(const ToolMouse& mouse);
    void released();
    void reset();

private:
    enum class Mode { None, MovePending, Move };

    ToolController& host_;
    Mode mode_ = Mode::None;
    plynth::Id strip_;
    int point_ = -1;
    QPoint pressView_;
    QPointF pressMetres_;
    std::vector<plynth::PlanXY> startPoints_;
    int key_ = 0;
};

// Draws light strips: each click adds a point of a line along a ceiling edge, a cabinet, a shelf or a wall. Enter,
// a right click or a click on the last point again ends the line, and a small menu asks for the kind (cove,
// soffit or edge). A click on a stair adds step lights to it. A click on a strip selects it and a drag moves it
// or one of its points. Delete removes the selected strip.
class LightStripTool : public Tool {
public:
    explicit LightStripTool(ToolController& host) : Tool(host), editor_(host) {}

    ToolId id() const override { return ToolId::LightStrip; }
    QString hint() const override;

    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseReleased(const ToolMouse& mouse) override;
    void mouseLeft() override;
    bool keyPressed(QKeyEvent* event) override;
    void floorChanged() override;
    void paint(QPainter& painter) override;

    // For checks: shows a line being drawn through these points, in metres, as clicks would.
    void showDraft(const std::vector<plynth::Vec2>& points);

private:
    void finish(const QPoint& global);
    void cancel();

    LightStripEditor editor_;
    std::vector<plynth::Vec2> draft_;
    std::optional<plynth::Vec2> hover_;
    bool menuOpen_ = false;
};

}
