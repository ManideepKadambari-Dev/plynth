#include "plynth/core/Stacking.h"

#include "plynth/core/Items.h"

#include "commands/StackSupport.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>
#include <string>

namespace plynth {

namespace {

// The items of a floor a dropped item could stand on at a plan point: floor and stacked items whose footprint
// holds the point, leaving out the moving item and everything standing on it.
std::vector<const Item*> targetsAt(const Floor& floor, double x, double y, std::string_view movingItem)
{
    std::set<Id> skipped;
    if (!movingItem.empty()) {
        skipped.insert(Id(movingItem));
        for (const Id& id : itemsStackedOn(floor, movingItem)) skipped.insert(id);
    }
    std::vector<const Item*> found;
    for (const Item& item : floor.items) {
        if (item.wall || skipped.count(item.id)) continue;
        if (footprintHoldsPoint(item, {x, y})) found.push_back(&item);
    }
    return found;
}

// The one of several items with the highest top; a later one wins a tie, as it is drawn above.
const Item* topmost(const std::vector<const Item*>& items)
{
    const Item* best = nullptr;
    for (const Item* item : items) {
        if (!best || itemTop(*item) >= itemTop(*best)) best = item;
    }
    return best;
}

}

// Says whether an item is a flat thing on the floor, such as a rug.
bool isUnderlay(const Item& item)
{
    return !item.wall && !item.on && item.size.height < kUnderlayHeight;
}

// Finds the item with the highest top under a plan point that a dropped item could stand on.
std::optional<Id> stackTargetAt(const Floor& floor, double x, double y, std::string_view movingItem)
{
    const Item* best = topmost(targetsAt(floor, x, y, movingItem));
    if (!best) return std::nullopt;
    return best->id;
}

// Finds the underlay a dropped item goes on by itself, when only underlays are under the point.
std::optional<Id> automaticStackTarget(const Floor& floor, double x, double y, std::string_view movingItem)
{
    const std::vector<const Item*> targets = targetsAt(floor, x, y, movingItem);
    if (targets.empty()) return std::nullopt;
    if (!std::all_of(targets.begin(), targets.end(), [](const Item* item) { return isUnderlay(*item); })) return std::nullopt;
    return topmost(targets)->id;
}

// Checks that one item may stand on another of the same floor.
Status checkStack(const Floor& floor, std::string_view item, std::string_view lower)
{
    const Item* upper = findItem(floor, item);
    if (!upper) return fail(ErrorCode::NotFound, "There is no item " + std::string(item) + " on this floor.");
    const Item* below = findItem(floor, lower);
    if (!below) return fail(ErrorCode::NotFound, "There is no item " + std::string(lower) + " on this floor.");
    if (upper->id == below->id) return fail(ErrorCode::InvalidValue, "The item " + upper->id + " cannot stand on itself.");
    if (upper->wall) {
        return fail(ErrorCode::InvalidValue, "The item " + upper->id + " hangs on a wall, so it cannot stand on another item.");
    }
    if (below->wall) {
        return fail(ErrorCode::InvalidValue, "The item " + below->id + " hangs on a wall, so nothing can stand on it.");
    }
    const std::vector<Id> above = itemsStackedOn(floor, item);
    if (std::find(above.begin(), above.end(), below->id) != above.end()) {
        return fail(ErrorCode::InvalidValue,
                    "The item " + below->id + " stands on " + upper->id + ", so " + upper->id + " cannot stand on it.");
    }
    return {};
}

// Says whether a plan point is on an item's footprint, turned with the item, edges included.
bool footprintHoldsPoint(const Item& item, Vec2 point)
{
    const double r = item.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    const double dx = point.x - item.x;
    const double dy = point.y - item.y;
    const double along = dx * c + dy * s;
    const double across = -dx * s + dy * c;
    return std::fabs(along) <= item.size.width / 2.0 + kItemTouchTolerance
        && std::fabs(across) <= item.size.depth / 2.0 + kItemTouchTolerance;
}

// Checks that an item with its centre at a plan point may stand on another item of the floor.
Status checkStandOnAt(const Plan& plan, const Floor& floor, std::string_view item, std::string_view lower, Vec2 point)
{
    const Id lowerId(lower);
    const Item* below = findItem(floor, lower);
    if (!below) {
        const Floor* other = findFloorContaining(plan, lower);
        if (other && findItem(*other, lower)) {
            return fail(ErrorCode::InvalidValue,
                        "The item " + lowerId + " is on floor " + other->id + ", not on floor " + floor.id + ".");
        }
        return fail(ErrorCode::NotFound, "There is no item " + lowerId + ".");
    }
    if (item.empty()) {
        if (below->wall) {
            return fail(ErrorCode::InvalidValue, "The item " + lowerId + " hangs on a wall, so nothing can stand on it.");
        }
    } else if (const Status s = checkStack(floor, item, lower); !s.ok()) {
        return s;
    }
    if (!footprintHoldsPoint(*below, point)) {
        char text[96];
        std::snprintf(text, sizeof text, "The centre (%g, %g) is not over the item ", point.x, point.y);
        return fail(ErrorCode::InvalidValue, text + lowerId + ".");
    }
    return {};
}

// Lists the items standing on an item, directly or on top of others, each after the one it stands on.
std::vector<Id> itemsStackedOn(const Floor& floor, std::string_view item)
{
    std::vector<Id> found;
    std::set<Id> seen{Id(item)};
    std::vector<Id> waiting{Id(item)};
    for (std::size_t next = 0; next < waiting.size(); ++next) {
        const Id current = waiting[next];
        for (const Item& other : floor.items) {
            if (!other.on || *other.on != current || seen.count(other.id)) continue;
            seen.insert(other.id);
            found.push_back(other.id);
            waiting.push_back(other.id);
        }
    }
    return found;
}

// The height of an item's top above the floor.
double itemTop(const Item& item)
{
    return item.z + item.size.height;
}

}
