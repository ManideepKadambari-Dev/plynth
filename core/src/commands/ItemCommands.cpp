#include "plynth/core/commands/ItemCommands.h"

#include "plynth/core/Items.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace plynth {

namespace {

// An item and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    Item* item = nullptr;
};

// Finds an item anywhere in the plan.
Located locate(Project& project, const Id& itemId)
{
    Located found;
    found.floor = findFloorContaining(project.plan, itemId);
    if (found.floor) found.item = findItem(*found.floor, itemId);
    if (!found.item) found.floor = nullptr;
    return found;
}

// The refusal for an item that is not there.
Status noItem(const Id& itemId)
{
    return fail(ErrorCode::NotFound, "There is no item " + itemId + ".");
}

// Puts a wall item flush on its wall again. The side and the place along the wall come from the item's
// position unless they are given.
Status poseOnWall(const Floor& floor, Item& item, std::optional<double> offset = {})
{
    const Wall* wall = findWall(floor, *item.wall);
    if (!wall) return fail(ErrorCode::NotFound, "There is no wall " + *item.wall + " on this floor.");
    const WallItemSpot spot = wallItemSpot(floor, item);
    if (!spot.valid) return fail(ErrorCode::InvalidValue, "The wall " + *item.wall + " has no valid shape.");
    const ItemPose pose = wallItemPose(floor, *wall, spot.side, offset.value_or(spot.offset), item.size);
    if (!pose.status.ok()) return pose.status;
    item.x = pose.position.x;
    item.y = pose.position.y;
    item.rotation = pose.rotation;
    return {};
}

// Refuses a wall item that would reach past either end of its wall when its centre is at a place along it.
Status checkWithinWall(const Floor& floor, const Item& item, double offset)
{
    const Wall* wall = findWall(floor, *item.wall);
    if (!wall) return fail(ErrorCode::NotFound, "There is no wall " + *item.wall + " on this floor.");
    const double length = wallLength(floor, *wall);
    const double half = item.size.width / 2.0;
    if (offset - half < -1e-6 || offset + half > length + 1e-6) {
        char text[160];
        std::snprintf(text, sizeof text, "The item is %.2f m wide. Centred %.2f m along wall %s, which is %.2f m long, it would reach past the end.",
                      item.size.width, offset, wall->id.c_str(), length);
        return fail(ErrorCode::InvalidValue, text);
    }
    return {};
}

// Refuses a wall item whose position, projected onto its wall, would put it past an end of the wall.
Status checkPositionWithinWall(const Floor& floor, const Item& item)
{
    const WallItemSpot spot = wallItemSpot(floor, item);
    if (!spot.valid) return fail(ErrorCode::InvalidValue, "The wall " + *item.wall + " has no valid shape.");
    return checkWithinWall(floor, item, spot.offset);
}

// Sets the room of an item from where it stands.
void setRoomFromPosition(const Floor& floor, Item& item)
{
    const Id room = roomAt(floor, {item.x, item.y});
    if (room.empty()) {
        item.room.reset();
    } else {
        item.room = room;
    }
}

// Finds the item in a floor by id and puts a copy of its old state back.
void restore(Project& project, const Id& floorId, const Item& previous)
{
    Floor* floor = findFloor(project.plan, floorId);
    if (!floor) return;
    if (Item* item = findItem(*floor, previous.id)) *item = previous;
}

}

PlaceItemCommand::PlaceItemCommand(Id floorId, Item item) : floorId_(std::move(floorId)), item_(std::move(item))
{
    item_.id.clear();
}

// The id of the new item, empty until the first apply.
const Id& PlaceItemCommand::itemId() const
{
    return item_.id;
}

std::string PlaceItemCommand::name() const
{
    return "Place item";
}

// Checks the item, puts a wall item flush, finds its room and adds it at the end of the list.
Status PlaceItemCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    Item item = item_;
    item.rotation = normalizeRotation(item.rotation);
    if (const Status s = checkItemPlacement(*floor, item); !s.ok()) return s;
    if (item.wall) {
        if (const Status s = checkPositionWithinWall(*floor, item); !s.ok()) return s;
        if (const Status s = poseOnWall(*floor, item); !s.ok()) return s;
    }
    if (!item.room) setRoomFromPosition(*floor, item);
    if (item.id.empty()) {
        item.id = newId(project.plan, kItemIdPrefix);
    } else if (idInUse(project.plan, item.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + item.id + " is already used.");
    }
    item_.id = item.id;
    floor->items.push_back(std::move(item));
    return {};
}

// Removes the item that apply added.
void PlaceItemCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->items, [&](const Item& i) { return i.id == item_.id; });
}

unsigned PlaceItemCommand::changes() const
{
    return Change::Items;
}

Id PlaceItemCommand::floorId() const
{
    return floorId_;
}

MoveItemCommand::MoveItemCommand(Id itemId, Vec2 position, int mergeKey)
    : itemId_(std::move(itemId)), position_(position), mergeKey_(mergeKey)
{
}

std::string MoveItemCommand::name() const
{
    return "Move item";
}

// Moves a floor item to the position, or slides a wall item along its wall to the place nearest it.
Status MoveItemCommand::apply(Project& project)
{
    const Located found = locate(project, itemId_);
    if (!found.item) return noItem(itemId_);
    if (!std::isfinite(position_.x) || !std::isfinite(position_.y)) {
        return fail(ErrorCode::InvalidValue, "The position must be finite numbers.");
    }
    Item moved = *found.item;
    moved.x = position_.x;
    moved.y = position_.y;
    if (moved.wall) {
        if (const Status s = poseOnWall(*found.floor, moved); !s.ok()) return s;
    }
    setRoomFromPosition(*found.floor, moved);
    if (const Status s = checkItemPlacement(*found.floor, moved); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.item;
    *found.item = std::move(moved);
    return {};
}

void MoveItemCommand::undo(Project& project)
{
    restore(project, floorId_, previous_);
}

unsigned MoveItemCommand::changes() const
{
    return Change::Items;
}

Id MoveItemCommand::floorId() const
{
    return floorId_;
}

// Folds a later move of the same item with the same key into this one.
bool MoveItemCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const MoveItemCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->itemId_ != itemId_) return false;
    position_ = other->position_;
    return true;
}

MoveItemOnWallCommand::MoveItemOnWallCommand(Id itemId, double offset, double z, int mergeKey)
    : itemId_(std::move(itemId)), offset_(offset), z_(z), mergeKey_(mergeKey)
{
}

std::string MoveItemOnWallCommand::name() const
{
    return "Move item on wall";
}

// Slides a wall item along its wall and sets the height of its bottom.
Status MoveItemOnWallCommand::apply(Project& project)
{
    const Located found = locate(project, itemId_);
    if (!found.item) return noItem(itemId_);
    if (!found.item->wall) return fail(ErrorCode::InvalidValue, "The item " + itemId_ + " is not on a wall.");
    if (!std::isfinite(offset_) || !std::isfinite(z_)) {
        return fail(ErrorCode::InvalidValue, "The offset and the height must be finite numbers.");
    }
    Item moved = *found.item;
    moved.z = z_;
    if (const Status s = checkItemPlacement(*found.floor, moved); !s.ok()) return s;
    if (const Status s = checkWithinWall(*found.floor, moved, offset_); !s.ok()) return s;
    if (const Status s = poseOnWall(*found.floor, moved, offset_); !s.ok()) return s;
    setRoomFromPosition(*found.floor, moved);
    floorId_ = found.floor->id;
    previous_ = *found.item;
    *found.item = std::move(moved);
    return {};
}

void MoveItemOnWallCommand::undo(Project& project)
{
    restore(project, floorId_, previous_);
}

unsigned MoveItemOnWallCommand::changes() const
{
    return Change::Items;
}

Id MoveItemOnWallCommand::floorId() const
{
    return floorId_;
}

// Folds a later move of the same item with the same key into this one.
bool MoveItemOnWallCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const MoveItemOnWallCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->itemId_ != itemId_) return false;
    offset_ = other->offset_;
    z_ = other->z_;
    return true;
}

SetItemHeightCommand::SetItemHeightCommand(Id itemId, double z, int mergeKey)
    : itemId_(std::move(itemId)), z_(z), mergeKey_(mergeKey)
{
}

std::string SetItemHeightCommand::name() const
{
    return "Set item height";
}

// Sets the height of the bottom of the item above the floor.
Status SetItemHeightCommand::apply(Project& project)
{
    const Located found = locate(project, itemId_);
    if (!found.item) return noItem(itemId_);
    Item changed = *found.item;
    changed.z = z_;
    if (const Status s = checkItemPlacement(*found.floor, changed); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = found.item->z;
    found.item->z = z_;
    return {};
}

void SetItemHeightCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Item* item = findItem(*floor, itemId_)) item->z = previous_;
}

unsigned SetItemHeightCommand::changes() const
{
    return Change::Items;
}

Id SetItemHeightCommand::floorId() const
{
    return floorId_;
}

// Folds a later change of the same item with the same key into this one.
bool SetItemHeightCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetItemHeightCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->itemId_ != itemId_) return false;
    z_ = other->z_;
    return true;
}

RotateItemCommand::RotateItemCommand(Id itemId, double rotation, int mergeKey)
    : itemId_(std::move(itemId)), rotation_(rotation), mergeKey_(mergeKey)
{
}

std::string RotateItemCommand::name() const
{
    return "Rotate item";
}

// Turns a floor item about its centre.
Status RotateItemCommand::apply(Project& project)
{
    const Located found = locate(project, itemId_);
    if (!found.item) return noItem(itemId_);
    if (!std::isfinite(rotation_)) return fail(ErrorCode::InvalidValue, "The rotation must be a finite number.");
    if (found.item->wall) {
        return fail(ErrorCode::InvalidValue, "The item " + itemId_ + " hangs on a wall and faces away from it. Take it off the wall first.");
    }
    floorId_ = found.floor->id;
    previous_ = *found.item;
    found.item->rotation = normalizeRotation(rotation_);
    return {};
}

void RotateItemCommand::undo(Project& project)
{
    restore(project, floorId_, previous_);
}

unsigned RotateItemCommand::changes() const
{
    return Change::Items;
}

Id RotateItemCommand::floorId() const
{
    return floorId_;
}

// Folds a later turn of the same item with the same key into this one.
bool RotateItemCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const RotateItemCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->itemId_ != itemId_) return false;
    rotation_ = other->rotation_;
    return true;
}

SwapItemAssetCommand::SwapItemAssetCommand(Id itemId, std::string asset, ItemSize size)
    : itemId_(std::move(itemId)), asset_(std::move(asset)), size_(size)
{
}

std::string SwapItemAssetCommand::name() const
{
    return "Swap item asset";
}

// Changes the asset and size. A wall item keeps its centre height and is put flush again for the new depth.
Status SwapItemAssetCommand::apply(Project& project)
{
    const Located found = locate(project, itemId_);
    if (!found.item) return noItem(itemId_);
    Item swapped = *found.item;
    swapped.asset = asset_;
    swapped.size = size_;
    Item plain = swapped;
    plain.wall.reset();
    if (const Status s = checkItemPlacement(*found.floor, plain); !s.ok()) return s;
    if (swapped.wall) {
        const double centre = itemCentreHeight(*found.item);
        swapped.z = std::max(0.0, centre - size_.height / 2.0);
        if (const Status s = checkItemPlacement(*found.floor, swapped); !s.ok()) return s;
        if (const Status s = poseOnWall(*found.floor, swapped); !s.ok()) return s;
        setRoomFromPosition(*found.floor, swapped);
    }
    floorId_ = found.floor->id;
    previous_ = *found.item;
    *found.item = std::move(swapped);
    return {};
}

void SwapItemAssetCommand::undo(Project& project)
{
    restore(project, floorId_, previous_);
}

unsigned SwapItemAssetCommand::changes() const
{
    return Change::Items;
}

Id SwapItemAssetCommand::floorId() const
{
    return floorId_;
}

SetItemWallCommand::SetItemWallCommand(Id itemId, std::optional<Id> wallId) : itemId_(std::move(itemId)), wallId_(std::move(wallId)) {}

std::string SetItemWallCommand::name() const
{
    return wallId_ ? "Mount item on wall" : "Take item off wall";
}

// Mounts the item flush on a wall, or takes it off and leaves it where it is.
Status SetItemWallCommand::apply(Project& project)
{
    const Located found = locate(project, itemId_);
    if (!found.item) return noItem(itemId_);
    Item changed = *found.item;
    if (wallId_) {
        if (!findWall(*found.floor, *wallId_)) {
            if (findFloorContaining(project.plan, *wallId_) && findWall(*findFloorContaining(project.plan, *wallId_), *wallId_)) {
                return fail(ErrorCode::InvalidValue, "The wall " + *wallId_ + " is on another floor than the item " + itemId_ + ".");
            }
            return fail(ErrorCode::NotFound, "There is no wall " + *wallId_ + ".");
        }
        changed.wall = wallId_;
        if (!(changed.z > 0.0)) changed.z = defaultWallItemZ(project.settings.defaults, changed.size.height);
        if (const Status s = checkItemPlacement(*found.floor, changed); !s.ok()) return s;
        if (const Status s = checkPositionWithinWall(*found.floor, changed); !s.ok()) return s;
        if (const Status s = poseOnWall(*found.floor, changed); !s.ok()) return s;
        setRoomFromPosition(*found.floor, changed);
    } else {
        changed.wall.reset();
    }
    floorId_ = found.floor->id;
    previous_ = *found.item;
    *found.item = std::move(changed);
    return {};
}

void SetItemWallCommand::undo(Project& project)
{
    restore(project, floorId_, previous_);
}

unsigned SetItemWallCommand::changes() const
{
    return Change::Items;
}

Id SetItemWallCommand::floorId() const
{
    return floorId_;
}

RemoveItemCommand::RemoveItemCommand(Id itemId) : itemId_(std::move(itemId)) {}

std::string RemoveItemCommand::name() const
{
    return "Remove item";
}

// Removes the item, remembering its place in the list.
Status RemoveItemCommand::apply(Project& project)
{
    const Located found = locate(project, itemId_);
    if (!found.item) return noItem(itemId_);
    floorId_ = found.floor->id;
    index_ = static_cast<std::size_t>(found.item - found.floor->items.data());
    removed_ = *found.item;
    found.floor->items.erase(found.floor->items.begin() + static_cast<std::ptrdiff_t>(index_));
    return {};
}

// Puts the item back at its old place.
void RemoveItemCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->items.size());
    floor->items.insert(floor->items.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveItemCommand::changes() const
{
    return Change::Items;
}

Id RemoveItemCommand::floorId() const
{
    return floorId_;
}

}
