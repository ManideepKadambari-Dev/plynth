#pragma once

#include "../Command.h"

#include <memory>
#include <span>
#include <vector>

namespace plynth {

// What a delete would remove, by kind, in the order the command removes them.
struct DeleteSet {
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

// Builds one undo step that removes the named points, walls and rooms and everything that would be left
// pointing at nothing: the walls at a removed point, the rooms that list a removed wall, and the end points of
// removed walls that no other wall uses any more. Points that had no wall to begin with stay unless named.
// Inside the group, rooms go first, then walls, then points. Build it on the plan it will run on and run it
// at once. InvalidValue for an empty list or ids on different floors, NotFound for an unknown id or one that
// is not a point, wall or room, InUse when an opening sits on a wall that would go (openings get their own
// delete in milestone 5).
DeleteBuild makeDeleteCommand(const Plan& plan, std::span<const Id> ids);

}
