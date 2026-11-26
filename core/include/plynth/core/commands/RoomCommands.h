#pragma once

#include "../Command.h"
#include "../Geometry.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace plynth {

// Adds a room for the closed area that holds a position, found with findClosedAreaAt. The room lists the
// area's walls in its order and gets the type when one is given. The new id comes from newId on the first
// apply and is kept for redo. NotFound for an unknown floor, or when no closed area holds the position.
// AlreadyExists when the position is inside an existing room (the message names it). InvalidValue for a
// position that is not finite or a type that is not a well formed name. The type is not checked against the
// room type list; callers use checkRoomType first. Items of the floor with no room whose position is inside the
// new room get it as their room, and lose it again on undo. Changes Rooms and Items.
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
    std::vector<Id> itemsGiven_; // items that got this room on apply
    std::vector<Id> lightsGiven_; // light markers that got this room on apply
};

// Removes a room. NotFound for an unknown room. Items in it lose their room. Undo puts the room back at the
// same place in the list and gives those items their room again. Changes Rooms and Items.
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
    std::vector<Id> itemsCleared_; // items that lost this room on apply
    std::vector<Id> lightsCleared_; // light markers that lost this room on apply
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

// Splits every room of the wall's floor that the wall cuts in two (roomSplitBy in Rooms.h), as one step. Each
// such room keeps its id for the part with the larger centre line area (the first in findClosedAreas order when
// both are equal), so the bigger part stays the room the user knows; the other part becomes a new room with the same type, materials and dressing setting, listed right after
// it. Items and light markers of the room that stand in the new part get the new room. A wall that cuts no room
// changes nothing. New ids come from newId on the first apply and are kept for redo. NotFound for an unknown wall.
// Undo puts the rooms, items and light markers back. Changes Rooms, Items and Lights.
class SplitRoomsByWallCommand : public Command {
public:
    explicit SplitRoomsByWallCommand(Id wallId);

    // The rooms the last apply made, in the order it made them.
    const std::vector<Id>& newRoomIds() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id wallId_;
    Id floorId_;
    std::vector<Id> newRoomIds_;
    std::vector<Id> idsForRedo_;
    std::vector<Room> previousRooms_;
    std::vector<Item> previousItems_;
    std::vector<LightMarker> previousLights_;
};

}
