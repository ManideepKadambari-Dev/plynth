#pragma once

#include "../Command.h"

#include <optional>
#include <string>
#include <vector>

namespace plynth {

// Stacking items (Stacking.h). Each is one undo step, the items it lifts or lowers included.

// Puts an item on another one (lower set) or back on the floor (lower empty): sets on, and z to the lower
// item's top or 0, and lifts every item stacked on it by the same change. NotFound for an unknown item,
// InvalidValue from checkStack. Moving an item onto the one it already stands on changes nothing, and so does
// putting one that stands on nothing (on the floor or on a wall) on the floor.
class SetItemOnCommand : public Command {
public:
    SetItemOnCommand(Id itemId, std::optional<Id> lower);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Items.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id itemId_;
    std::optional<Id> lower_;
    Id floorId_;
    std::vector<Item> previous_; // every item the command changed, as it was
};

// The "under it" choice: the dropped item stays on the floor and the other item goes on it. The same as
// SetItemOnCommand(other, dropped), named "Put under", except when the dropped item stood on the other one:
// then it first takes the other item's place (on and z), so the two change places.
class PutItemUnderCommand : public Command {
public:
    PutItemUnderCommand(Id droppedId, Id otherId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    // Items.
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id droppedId_;
    Id otherId_;
    Id floorId_;
    std::vector<Item> previous_;
};

}
