#include "plynth/core/commands/StackCommands.h"

#include "plynth/core/Stacking.h"

#include "StackSupport.h"

#include <algorithm>
#include <utility>

namespace plynth {

namespace {

// Refuses a lower item that is on another floor than the item, before checkStack says it is missing.
Status checkSameFloor(const Plan& plan, const Floor& floor, const Id& itemId, const Id& lowerId)
{
    const Floor* other = findFloorContaining(plan, lowerId);
    if (other && other != &floor && findItem(*other, lowerId)) {
        return fail(ErrorCode::InvalidValue, "The item " + lowerId + " is on another floor than the item " + itemId + ".");
    }
    return {};
}

// Finds the floor that holds an item. Null when there is no such item.
Floor* floorOfItem(Plan& plan, const Id& itemId)
{
    Floor* floor = findFloorContaining(plan, itemId);
    return floor && findItem(*floor, itemId) ? floor : nullptr;
}

// Puts an item on another one or on the floor and brings what stands on it along. Each changed item is saved.
void standOn(Floor& floor, const Id& itemId, const std::optional<Id>& lower, std::vector<Item>& saved)
{
    Item* item = findItem(floor, itemId);
    const Item* below = lower ? findItem(floor, *lower) : nullptr;
    const Item before = *item;
    if (!lower && !before.on) return;
    Item changed = before;
    changed.on = lower;
    changed.z = below ? itemTop(*below) : 0.0;
    if (changed == before) return;
    detail::keepOld(saved, before);
    *item = std::move(changed);
    detail::carryStack(floor, before, saved);
}

}

SetItemOnCommand::SetItemOnCommand(Id itemId, std::optional<Id> lower) : itemId_(std::move(itemId)), lower_(std::move(lower)) {}

std::string SetItemOnCommand::name() const
{
    return lower_ ? "Put on item" : "Put on floor";
}

// Puts the item on the lower item, or back on the floor, and lifts or lowers what stands on it.
Status SetItemOnCommand::apply(Project& project)
{
    Floor* floor = floorOfItem(project.plan, itemId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no item " + itemId_ + ".");
    if (lower_) {
        if (const Status s = checkSameFloor(project.plan, *floor, itemId_, *lower_); !s.ok()) return s;
        if (const Status s = checkStack(*floor, itemId_, *lower_); !s.ok()) return s;
    }
    floorId_ = floor->id;
    previous_.clear();
    standOn(*floor, itemId_, lower_, previous_);
    return {};
}

void SetItemOnCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) detail::restoreItems(*floor, previous_);
}

unsigned SetItemOnCommand::changes() const
{
    return Change::Items;
}

Id SetItemOnCommand::floorId() const
{
    return floorId_;
}

PutItemUnderCommand::PutItemUnderCommand(Id droppedId, Id otherId) : droppedId_(std::move(droppedId)), otherId_(std::move(otherId)) {}

std::string PutItemUnderCommand::name() const
{
    return "Put under";
}

// Puts the other item on the dropped one. When the dropped item stood on the other one, it first takes the other
// item's place, so the two change places.
Status PutItemUnderCommand::apply(Project& project)
{
    Floor* floor = floorOfItem(project.plan, droppedId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no item " + droppedId_ + ".");
    if (!floorOfItem(project.plan, otherId_)) return fail(ErrorCode::NotFound, "There is no item " + otherId_ + ".");
    if (const Status s = checkSameFloor(project.plan, *floor, droppedId_, otherId_); !s.ok()) return s;

    const std::vector<Id> above = itemsStackedOn(*floor, otherId_);
    const bool droppedAbove = std::find(above.begin(), above.end(), droppedId_) != above.end();
    if (!droppedAbove) {
        if (const Status s = checkStack(*floor, otherId_, droppedId_); !s.ok()) return s;
    } else if (const Item* other = findItem(*floor, otherId_); other->wall) {
        return fail(ErrorCode::InvalidValue, "The item " + otherId_ + " hangs on a wall, so it cannot stand on another item.");
    }

    floorId_ = floor->id;
    previous_.clear();
    if (droppedAbove) {
        const std::optional<Id> base = findItem(*floor, otherId_)->on;
        standOn(*floor, droppedId_, base, previous_);
    }
    standOn(*floor, otherId_, droppedId_, previous_);
    return {};
}

void PutItemUnderCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) detail::restoreItems(*floor, previous_);
}

unsigned PutItemUnderCommand::changes() const
{
    return Change::Items;
}

Id PutItemUnderCommand::floorId() const
{
    return floorId_;
}

}
