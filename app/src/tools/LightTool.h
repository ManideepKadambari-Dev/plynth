#pragma once

#include "tools/Tool.h"

#include <plynth/core/Plan.h>

#include <QPoint>
#include <QPointF>

namespace plynth::app {

// Selects and drags the light markers of the shown floor. The Light tool and the Select tool both use it. A
// drag is MoveLightCommand runs with one merge key, so one drag is one undo step.
class LightEditor {
public:
    explicit LightEditor(ToolController& host) : host_(host) {}

    // True while a light is pressed or dragged.
    bool busy() const { return mode_ != Mode::None; }
    // A press on a light marker selects it and prepares a move. Returns true when it did.
    bool pressBody(const ToolMouse& mouse);
    void moved(const ToolMouse& mouse);
    void released();
    void reset();

private:
    enum class Mode { None, MovePending, Move };

    ToolController& host_;
    Mode mode_ = Mode::None;
    plynth::Id light_;
    QPoint pressView_;
    QPointF grab_;
    int key_ = 0;
};

// Places light markers: a click on an empty spot adds a ceiling light at its start height and selects it.
// A click on a marker selects it and a drag moves it. Delete removes the selected light.
class LightTool : public Tool {
public:
    explicit LightTool(ToolController& host) : Tool(host), editor_(host) {}

    ToolId id() const override { return ToolId::Light; }
    QString hint() const override;

    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseReleased(const ToolMouse& mouse) override;
    bool keyPressed(QKeyEvent* event) override;
    void floorChanged() override;

private:
    LightEditor editor_;
};

}
