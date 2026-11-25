#pragma once

#include "tools/Tool.h"

#include <plynth/core/Geometry.h>
#include <plynth/core/Plan.h>

#include <QPoint>
#include <QPointF>
#include <QString>

#include <optional>
#include <string>

namespace plynth::app {

// Moves and turns the selected stair of the shown floor. The Select tool uses it: a handle at the bottom step
// moves the stair, a handle beyond the top turns it, and dragging the stair itself moves it. Every change is a
// MoveStairCommand, and one drag is one undo step.
class StairEditor {
public:
    explicit StairEditor(ToolController& host) : host_(host) {}

    // True while a drag is in progress.
    bool busy() const { return mode_ != Mode::None; }
    // A press on a handle of the selected stair starts a move or a turn. Returns true when it did.
    bool pressHandle(const ToolMouse& mouse);
    // A press on a stair that no other mark covers selects it and prepares a move. Returns true when it did.
    bool pressBody(const ToolMouse& mouse);
    void moved(const ToolMouse& mouse);
    void released();
    // True when the cursor is over a handle.
    bool overHandle(const QPoint& view) const;
    void paintHandles(QPainter& painter, bool hover) const;
    void reset();

private:
    enum class Mode { None, Move, MovePending, Rotate };

    const plynth::Stair* selected() const;
    std::optional<QPointF> moveHandleView() const;
    std::optional<QPointF> rotateHandleView() const;
    void moveTo(const ToolMouse& mouse);
    void rotateTo(const ToolMouse& mouse);

    ToolController& host_;
    Mode mode_ = Mode::None;
    plynth::Id stair_;
    QPoint pressView_;
    QPointF grab_;
    double angleGrab_ = 0.0;
    int key_ = 0;
};

// Places stairs: click the bottom step and drag toward the top. A live preview follows, snapping to 15 degrees
// (Alt for free). On release a small menu lists the stair types, and picking one adds the stair.
class StairTool : public Tool {
public:
    explicit StairTool(ToolController& host) : Tool(host) {}

    ToolId id() const override { return ToolId::Stair; }
    QString hint() const override;

    void activate() override;
    void deactivate() override;
    void mousePressed(const ToolMouse& mouse) override;
    void mouseMoved(const ToolMouse& mouse) override;
    void mouseReleased(const ToolMouse& mouse) override;
    bool keyPressed(QKeyEvent* event) override;
    void modifiersChanged() override;
    void floorChanged() override;
    void documentChanged(unsigned changes) override;
    void paint(QPainter& painter) override;

    // The tooltip of the tool strip button.
    static QString tooltipText();

    // Shows the live preview of a drag from one place to another, in metres, as the mouse would. For checks.
    void previewDrag(plynth::Vec2 from, plynth::Vec2 to);
    // Adds the stair being dragged with a type. False when nothing is being dragged or the command is refused.
    bool placeAs(const std::string& type);

private:
    plynth::Vec2 snapStart(const ToolMouse& mouse) const;
    void updateEnd(const ToolMouse& mouse);
    std::optional<plynth::Stair> stairNow(const std::string& type) const;
    void chooseType(const QPoint& global);
    void cancel();

    bool dragging_ = false;
    plynth::Vec2 start_;
    std::optional<plynth::Vec2> end_;
    bool free_ = false;
    std::string previewType_ = "straight";
    bool menuOpen_ = false;
};

}
