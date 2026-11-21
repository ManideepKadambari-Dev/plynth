#pragma once

#include "../Command.h"
#include "../Geometry.h"

#include <cstddef>
#include <optional>
#include <string>

namespace plynth {

// Every item command checks the result with checkItemPlacement (Items.h) before changing anything and refuses
// with what it says. Fit and overlap problems never refuse; front ends show them from checkItem. Commands do
// not know the library: the app and the CLI look the asset up with itemAssetFor (catalog/ItemAssets.h) before
// building PlaceItemCommand or SwapItemAssetCommand, and refuse a missing or unmeasured asset themselves.
// After each change of position the item's room is roomAt its position (empty when in no room). A wall item is
// always put flush on its wall again with wallItemPose, on the side its position is on, at the place along the
// wall nearest its position. Moves with the same non zero mergeKey and item merge into one undo step; the app
// takes every key from ToolController::nextMergeKey. Every command here changes Items only.

// Adds an item at the end of the floor's item list. item.id is ignored: the new id comes from newId with
// kItemIdPrefix on the first apply and is kept for redo. rotation is normalized. When item.room is set it must
// be a room of the floor and is kept; otherwise it is roomAt the position. A wall item is put flush as above,
// with z as given. NotFound for an unknown floor, otherwise as checkItemPlacement.
class PlaceItemCommand : public Command {
public:
    PlaceItemCommand(Id floorId, Item item);

    // The id of the new item. Empty until apply has succeeded once.
    const Id& itemId() const;

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id floorId_;
    Item item_;
};

// Moves an item on the plan. A floor item goes to the position as given (the caller snaps it first with
// snapItemPosition). A wall item slides along its wall to the place nearest the position and stays flush.
// z and rotation stay. NotFound for an unknown item, InvalidValue for a position that is not finite.
class MoveItemCommand : public Command {
public:
    MoveItemCommand(Id itemId, Vec2 position, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id itemId_;
    Vec2 position_;
    int mergeKey_;
    Id floorId_;
    Item previous_;
};

// Moves a wall item on its wall face, for the wall view: offset along the wall's true centre line to its
// centre, and z for its bottom. NotFound for an unknown item, InvalidValue for an item that is not on a wall,
// otherwise as wallItemPose and checkItemPlacement (for example its top above the wall).
class MoveItemOnWallCommand : public Command {
public:
    MoveItemOnWallCommand(Id itemId, double offset, double z, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id itemId_;
    double offset_;
    double z_;
    int mergeKey_;
    Id floorId_;
    Item previous_;
};

// Sets the height of an item's bottom above the floor, for floor and wall items. NotFound for an unknown item,
// otherwise as checkItemPlacement.
class SetItemHeightCommand : public Command {
public:
    SetItemHeightCommand(Id itemId, double z, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id itemId_;
    double z_;
    int mergeKey_;
    Id floorId_;
    double previous_ = 0.0;
};

// Turns a floor item about its centre to a rotation in degrees (normalized). The position stays. NotFound for
// an unknown item, InvalidValue for a rotation that is not finite or for a wall item (it faces away from its
// wall; detach it first).
class RotateItemCommand : public Command {
public:
    RotateItemCommand(Id itemId, double rotation, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id itemId_;
    double rotation_;
    int mergeKey_;
    Id floorId_;
    Item previous_;
};

// Puts another asset in an item's place, or the same asset with its new size from the library. Position and
// rotation stay; a wall item is put flush again for the new depth and keeps its centre height (z becomes the
// old centre minus half the new height, never below 0). Id and room stay. NotFound for an unknown item,
// otherwise as checkItemPlacement.
class SwapItemAssetCommand : public Command {
public:
    SwapItemAssetCommand(Id itemId, std::string asset, ItemSize size);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id itemId_;
    std::string asset_;
    ItemSize size_;
    Id floorId_;
    Item previous_;
};

// Mounts an item on a wall of its floor, or takes it off. Mounting puts it flush on the side of the wall its
// position is on, at the nearest place along it; z stays when above 0, otherwise it is defaultWallItemZ from
// the project's default sizes. Taking it off clears wall and keeps position, rotation and z. NotFound for an
// unknown item or wall, InvalidValue for a wall on another floor, otherwise as checkItemPlacement.
class SetItemWallCommand : public Command {
public:
    SetItemWallCommand(Id itemId, std::optional<Id> wallId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id itemId_;
    std::optional<Id> wallId_;
    Id floorId_;
    Item previous_;
};

// Removes an item. NotFound for an unknown item. Undo puts it back at the same place in the list.
class RemoveItemCommand : public Command {
public:
    explicit RemoveItemCommand(Id itemId);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;

private:
    Id itemId_;
    Id floorId_;
    Item removed_;
    std::size_t index_ = 0;
};

}
