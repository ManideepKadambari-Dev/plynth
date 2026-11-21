#pragma once

#include "tools/Tool.h"

#include <optional>
#include <string>

class QMenu;

namespace plynth::app {

// Turns a closed area into a room, or changes the type of a room, by clicking inside it.
class RoomTool : public Tool {
public:
    explicit RoomTool(ToolController& host) : Tool(host) {}

    ToolId id() const override { return ToolId::Room; }
    QString hint() const override;

    void mousePressed(const ToolMouse& mouse) override;

    // Shows the room type menu at a screen position. Returns false when the menu was closed without a pick.
    // type is empty for "No type yet".
    static bool pickType(const std::optional<std::string>& current, const QPoint& global,
                         std::optional<std::string>* type);
};

}
