#pragma once

#include <QCursor>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <Qt>

#include <array>

class QKeyEvent;
class QPainter;

namespace plynth::app {

class ToolController;

enum class ToolId { Select, Scale, Wall, Opening, Room, Detect };

// Every tool, in the order of the tool strip and the Edit menu.
inline constexpr std::array<ToolId, 6> kAllTools{ToolId::Select, ToolId::Scale, ToolId::Wall,
                                                 ToolId::Opening, ToolId::Room, ToolId::Detect};

// A mouse event handed to a tool. scene is in image pixels, view in canvas pixels.
struct ToolMouse {
    QPointF scene;
    QPoint view;
    QPoint global;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::KeyboardModifiers modifiers;
};

// What every tool can do. A tool changes the plan only through ToolController::run.
class Tool {
public:
    explicit Tool(ToolController& host) : host_(host) {}
    virtual ~Tool() = default;

    virtual ToolId id() const = 0;
    virtual QString hint() const = 0;
    virtual Qt::CursorShape cursor() const { return Qt::CrossCursor; }

    virtual void activate() {}
    virtual void deactivate() {}
    virtual void mousePressed(const ToolMouse&) {}
    virtual void mouseMoved(const ToolMouse&) {}
    virtual void mouseReleased(const ToolMouse&) {}
    virtual void mouseLeft() {}
    // Returns true when the key was used.
    virtual bool keyPressed(QKeyEvent*) { return false; }
    // Shift or Control went down or up, so a preview may need to change.
    virtual void modifiersChanged() {}
    // The document changed. byTool is true while this tool's own command is running.
    virtual void documentChanged(unsigned changes) { Q_UNUSED(changes); }
    // Draws previews over the plan. The painter works in canvas pixels.
    virtual void paint(QPainter&) {}

protected:
    ToolController& host_;
};

}
