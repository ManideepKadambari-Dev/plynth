#pragma once

#include "../Command.h"

#include <memory>
#include <span>
#include <vector>

namespace plynth {

// What a delete would remove, by kind, in the order the command removes them.
struct DeleteSet {
    std::vector<Id> items;     // items named in the list, removed
    std::vector<Id> detached;  // wall items on a removed wall, taken off it and kept where they are
    std::vector<Id> openings;
    std::vector<Id> rooms;
    std::vector<Id> walls;
    std::vector<Id> points;
};

// A delete command ready to run, or the reason it cannot be built.
struct DeleteBuild {
    Status status;
    std::unique_ptr<Command> command; // a CommandGroup named "Delete", null on error
    DeleteSet removes;
};

// Builds one undo step that removes the named items, openings, points, walls and rooms and everything that
// would be left pointing at nothing: the walls at a removed point, the openings on a removed wall, the rooms
// that list a removed wall, and the end points of removed walls that no other wall uses any more. Items are
// never removed unless named: a wall item on a removed wall is taken off it (SetItemWallCommand with no wall)
// and keeps its place and height, and items in a removed room lose their room (RemoveRoomCommand does that).
// Points that had no wall to begin with stay unless named. Inside the group, named items go first
// (RemoveItemCommand), then detached items, then openings (RemoveOpeningCommand), then rooms, then walls, then
// points, each in the floor's list order. Build it on the plan it will run on and run it at once.
// InvalidValue for an empty list or ids on different floors, NotFound for an unknown id or one that is not an
// item, opening, point, wall or room.
DeleteBuild makeDeleteCommand(const Plan& plan, std::span<const Id> ids);

}
