#pragma once

#include "../Command.h"
#include "../Geometry.h"

#include <cstddef>
#include <optional>
#include <string>

namespace plynth {

// Adds a room for the closed area that holds a position, found with findClosedAreaAt. The room lists the
// area's walls in its order and gets the type when one is given. The new id comes from newId on the first
// apply and is kept for redo. NotFound for an unknown floor, or when no closed area holds the position.
// AlreadyExists when the position is inside an existing room (the message names it). InvalidValue for a
// position that is not finite or a type that is not a well formed name. The type is not checked against the
// room type list; callers use checkRoomType first.
class AddRoomCommand : public Command {
public:
    AddRoomCommand(Id floorId, Vec2 position, std::optional<std::string> type = {});

    // The id of the new room. Empty until apply has succeeded once.
    const Id& roomId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Vec2 position_;
    Room room_;
};

// Removes a room. NotFound for an unknown room. Undo puts it back at the same place in the list.
class RemoveRoomCommand : public Command {
public:
    explicit RemoveRoomCommand(Id roomId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id roomId_;
    Id floorId_;
    Room removed_;
    std::size_t index_ = 0;
};

// Sets or clears the type of a room. NotFound for an unknown room, InvalidValue for a type that is not a
// well formed name. As with AddRoomCommand, callers check the type with checkRoomType first.
class SetRoomTypeCommand : public Command {
public:
    SetRoomTypeCommand(Id roomId, std::optional<std::string> type);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id roomId_;
    Id floorId_;
    std::optional<std::string> type_;
    std::optional<std::string> previous_;
};

}
