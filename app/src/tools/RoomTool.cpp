#include "RoomTool.h"

#include "document/Selection.h"
#include "tools/ToolController.h"

#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/commands/RoomCommands.h>

#include <QAction>
#include <QMenu>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// The status bar hint for this tool.
QString RoomTool::hint() const
{
    return QObject::tr("Click inside a closed area to make a room, or inside a room to change its type.");
}

// Shows the built in room types and "No type yet", with the current one ticked.
bool RoomTool::pickType(const std::optional<std::string>& current, const QPoint& global,
                        std::optional<std::string>* type)
{
    QMenu menu;
    menu.setToolTipsVisible(true);
    const auto add = [&](const QString& text, const std::optional<std::string>& value) {
        QAction* action = menu.addAction(text);
        action->setCheckable(true);
        action->setChecked(value == current);
        action->setData(value.has_value() ? QVariant(QString::fromStdString(*value)) : QVariant());
        action->setToolTip(QObject::tr("Use this room type"));
        return action;
    };
    const bool known = !current.has_value() || plynth::findRoomType(*current) != nullptr;
    if (!known)
        add(QString::fromStdString(*current), current);
    for (const plynth::RoomType& roomType : plynth::builtInRoomTypes())
        add(fromCore(roomType.name), std::string(roomType.id));
    menu.addSeparator();
    add(QObject::tr("No type yet"), std::nullopt);
    QAction* chosen = menu.exec(global);
    if (chosen == nullptr)
        return false;
    const QVariant data = chosen->data();
    *type = data.isValid() && !data.isNull() ? std::optional<std::string>(data.toString().toStdString())
                                             : std::nullopt;
    return true;
}

// Selects the room under the click and offers its types, or offers the types for a new room.
void RoomTool::mousePressed(const ToolMouse& mouse)
{
    if (mouse.button != Qt::LeftButton)
        return;
    const plynth::Floor* floor = host_.floor();
    const std::optional<double> ppm = host_.pixelsPerMetre();
    if (floor == nullptr || !ppm.has_value())
        return;
    const plynth::Vec2 at{mouse.scene.x() / *ppm, mouse.scene.y() / *ppm};

    const plynth::Id roomId = plynth::roomAt(*floor, at);
    if (!roomId.empty()) {
        host_.selection().set(SelectionKind::Room, QString::fromStdString(roomId));
        std::optional<std::string> current;
        for (const plynth::Room& room : floor->rooms)
            if (room.id == roomId)
                current = room.type;
        std::optional<std::string> picked;
        if (pickType(current, mouse.global, &picked) && picked != current)
            host_.run(std::make_unique<plynth::SetRoomTypeCommand>(roomId, picked));
        return;
    }

    if (!plynth::findClosedAreaAt(*floor, at).has_value()) {
        host_.showMessage(QObject::tr("No closed area here. Close the walls around it first."));
        return;
    }
    std::optional<std::string> picked;
    if (!pickType(std::nullopt, mouse.global, &picked))
        return;
    auto command = std::make_unique<plynth::AddRoomCommand>(floor->id, at, picked);
    plynth::AddRoomCommand* raw = command.get();
    if (host_.run(std::move(command)).ok())
        host_.selection().set(SelectionKind::Room, QString::fromStdString(raw->roomId()));
}

}
