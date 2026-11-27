#pragma once

#include <plynth/core/Command.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/Plan.h>
#include <plynth/core/Status.h>

#include <memory>
#include <optional>
#include <string>

namespace plynth::cli {

// What --on asked for on place and item move (Stacking.h has the rules). given is false when the option was
// left out; lower is empty with given true for "--on none", back on the floor.
struct OnChoice {
    bool given = false;
    std::optional<Id> lower;
};

// Reads the text of --on: an item id, or "none" for the floor. No text gives a choice that is not given.
OnChoice onChoiceFrom(const std::optional<std::string>& text);

// Checks that an item with its centre at a plan point may stand on another item of its floor, with
// checkStandOnAt (Stacking.h). Adds a hint about --x and --y when the point is not over it.
Status checkStandOn(const Plan& plan, const Floor& floor, const Id& itemId, const Id& lower, Vec2 at);

// Decides what a new floor item stands on and sets item.on: the item named by --on, nothing for --on none, and
// with --on left out the underlay under its centre (automaticStackTarget) unless its height was given.
// item.x and item.y must be set. A wall item never stands on anything.
Status chooseStackForPlace(const Plan& plan, const Floor& floor, const OnChoice& on, bool heightGiven, Item& item);

// What an item move asks for. position is already snapped. Everything left out stays.
struct ItemMoveRequest {
    std::optional<Vec2> position;
    std::optional<double> offset;
    std::optional<double> z;
    OnChoice on;
};

// An item move ready to run, or why it cannot be made.
struct ItemMoveBuild {
    Status status;
    std::unique_ptr<Command> command;
};

// Builds an item move as one undo step: the move along the plan or the wall, then what it stands on, then its
// height. With --on left out, a floor item moved on the plan to a place over an underlay goes on it by itself,
// unless a height was given. The stack is checked first, so a refusal changes nothing.
ItemMoveBuild buildItemMove(const Plan& plan, const Id& itemId, const ItemMoveRequest& request);

}
