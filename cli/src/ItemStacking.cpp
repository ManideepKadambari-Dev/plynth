#include "ItemStacking.h"

#include <plynth/core/Items.h>
#include <plynth/core/Stacking.h>
#include <plynth/core/commands/ItemCommands.h>
#include <plynth/core/commands/StackCommands.h>

#include <utility>

namespace plynth::cli {

// Reads the text of --on.
OnChoice onChoiceFrom(const std::optional<std::string>& text) {
    OnChoice choice;
    if (!text) {
        return choice;
    }
    choice.given = true;
    if (*text != "none") {
        choice.lower = *text;
    }
    return choice;
}

// Checks that an item at a plan point may stand on another item of the floor (checkStandOnAt in core), with a
// hint about --x and --y when the point is not over it.
Status checkStandOn(const Plan& plan, const Floor& floor, const Id& itemId, const Id& lower, Vec2 at) {
    Status checked = checkStandOnAt(plan, floor, itemId, lower, at);
    if (checked.code == ErrorCode::InvalidValue && checked.message.rfind("The centre", 0) == 0) {
        checked.message += " Give --x and --y inside its footprint.";
    }
    return checked;
}

// Decides what a new floor item stands on.
Status chooseStackForPlace(const Plan& plan, const Floor& floor, const OnChoice& on, bool heightGiven, Item& item) {
    item.on.reset();
    if (item.wall) {
        return {};
    }
    if (on.given) {
        if (on.lower) {
            const Status checked = checkStandOn(plan, floor, Id(), *on.lower, {item.x, item.y});
            if (!checked.ok()) {
                return checked;
            }
            item.on = on.lower;
        }
        return {};
    }
    if (!heightGiven) {
        item.on = automaticStackTarget(floor, item.x, item.y, "");
    }
    return {};
}

// Builds an item move with its stacking as one command group.
ItemMoveBuild buildItemMove(const Plan& plan, const Id& itemId, const ItemMoveRequest& request) {
    ItemMoveBuild build;
    const Floor* floor = findFloorContaining(plan, itemId);
    const Item* item = floor == nullptr ? nullptr : findItem(*floor, itemId);
    if (item == nullptr) {
        build.status = fail(ErrorCode::NotFound, "There is no item " + itemId + ".");
        return build;
    }
    const Vec2 at = request.position.value_or(Vec2{item->x, item->y});

    std::optional<std::optional<Id>> standOn;
    if (request.on.given) {
        if (request.on.lower) {
            build.status = checkStandOn(plan, *floor, itemId, *request.on.lower, at);
            if (!build.status.ok()) {
                return build;
            }
        }
        standOn.emplace(request.on.lower);
    } else if (request.position && !request.z && !item->wall) {
        const std::optional<Id> underlay = automaticStackTarget(*floor, at.x, at.y, itemId);
        if (underlay && underlay != item->on) {
            standOn.emplace(underlay);
        }
    }

    auto group = std::make_unique<CommandGroup>("Move item");
    if (request.offset) {
        group->add(std::make_unique<MoveItemOnWallCommand>(itemId, *request.offset, request.z.value_or(item->z)));
    } else if (request.position) {
        group->add(std::make_unique<MoveItemCommand>(itemId, *request.position));
    }
    if (standOn) {
        group->add(std::make_unique<SetItemOnCommand>(itemId, *standOn));
    }
    if (request.z && !request.offset) {
        group->add(std::make_unique<SetItemHeightCommand>(itemId, *request.z));
    }
    build.command = std::move(group);
    return build;
}

}
