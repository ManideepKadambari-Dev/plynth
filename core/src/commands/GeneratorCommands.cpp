#include <plynth/core/commands/GeneratorCommands.h>

#include "plynth/core/Items.h"
#include "plynth/core/Rooms.h"

#include <algorithm>
#include <utility>

namespace plynth {

SetItemGeneratorCommand::SetItemGeneratorCommand(Id itemId, GeneratorUse use, ItemSize size, int mergeKey)
    : itemId_(std::move(itemId)), use_(std::move(use)), size_(size), mergeKey_(mergeKey)
{
}

std::string SetItemGeneratorCommand::name() const
{
    return "Change generator values";
}

// Sets the generator, its values and the size. A wall item is put flush again for the new depth.
Status SetItemGeneratorCommand::apply(Project& project)
{
    Floor* floor = findFloorContaining(project.plan, itemId_);
    Item* item = floor ? findItem(*floor, itemId_) : nullptr;
    if (!item) return fail(ErrorCode::NotFound, "There is no item " + itemId_ + ".");
    Item changed = *item;
    changed.asset.clear();
    changed.generator = use_;
    changed.size = size_;
    if (changed.wall) {
        Item plain = changed;
        plain.wall.reset();
        if (const Status s = checkItemPlacement(*floor, plain); !s.ok()) return s;
        if (const Status s = checkItemPlacement(*floor, changed); !s.ok()) return s;
        const Wall* wall = findWall(*floor, *changed.wall);
        const WallItemSpot spot = wallItemSpot(*floor, *item);
        if (!wall || !spot.valid) return fail(ErrorCode::InvalidValue, "The wall " + *changed.wall + " has no valid shape.");
        const ItemPose pose = wallItemPose(*floor, *wall, spot.side, spot.offset, size_);
        if (!pose.status.ok()) return pose.status;
        changed.x = pose.position.x;
        changed.y = pose.position.y;
        changed.rotation = pose.rotation;
        if (const Status s = checkItemPlacement(*floor, changed); !s.ok()) return s;
    } else if (const Status s = checkItemPlacement(*floor, changed); !s.ok()) {
        return s;
    }
    floorId_ = floor->id;
    previous_ = *item;
    *item = std::move(changed);
    return {};
}

void SetItemGeneratorCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    Item* item = floor ? findItem(*floor, previous_.id) : nullptr;
    if (item) *item = previous_;
}

unsigned SetItemGeneratorCommand::changes() const
{
    return Change::Items;
}

Id SetItemGeneratorCommand::floorId() const
{
    return floorId_;
}

// Folds a later change of the same item with the same key into this one.
bool SetItemGeneratorCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetItemGeneratorCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->itemId_ != itemId_) return false;
    use_ = other->use_;
    size_ = other->size_;
    return true;
}

}
