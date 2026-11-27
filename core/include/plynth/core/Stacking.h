#pragma once

#include "Plan.h"
#include "Status.h"

#include <optional>
#include <string_view>
#include <vector>

namespace plynth {

// Stacking items (issue 32, plan format 8 Item::on). An item may stand on another item of its floor: a sofa on a
// rug, a lamp on a table. It then stands at the lower item's top (z = lower z + lower height), and the item
// commands keep it there: moving the lower item moves the items on it by the same amount (turning it turns them
// round its centre), resizing or swapping it lifts them to its new top, removing it or hanging it on a wall lets
// them down to where it stood (the item it stood on, or the floor). An item moved so that its centre leaves the
// item it stands on goes back on the floor, and one given a height other than that item's top no longer stands
// on it. The build places each item at its z, so it needs nothing new.
//
// Underlays: a floor item lower than kUnderlayHeight (rugs, carpets, mats) is an underlay. An item placed or moved
// so that its footprint's centre lies over an underlay goes on it by itself, with no question and no overlap
// warning between the two. The front ends do this with automaticStackTarget: item.on on PlaceItemCommand, or
// SetItemOnCommand in one group with the move; the item commands never stack an item by themselves. For any
// other item under the drop point the front end asks: on top of it, under it (the dropped item stays on the
// floor and the other goes on it), or both on the floor. The same choice is on the item's right click menu, and
// "plynth place" and "plynth item move" take --on <item id> (or --on none).
// itemProblems (Items.h) gives no overlaps_item warning between an item and the item it stands on, or between
// an underlay and an item on the floor over it.

inline constexpr double kUnderlayHeight = 0.03; // metres

// Says whether an item counts as an underlay: a floor item (no wall, not on anything) lower than kUnderlayHeight.
bool isUnderlay(const Item& item);

// The item of a floor under a plan point that a dropped item could stand on: the topmost (highest top) floor or
// stacked item whose footprint holds the point, leaving out the item itself and every item standing on it, at any
// depth. Empty when there is none.
std::optional<Id> stackTargetAt(const Floor& floor, double x, double y, std::string_view movingItem);

// What a drop should do by itself: the underlay under the point when the only target is an underlay. Empty when
// the front end must ask (another kind of item is under the point) or when nothing is there.
std::optional<Id> automaticStackTarget(const Floor& floor, double x, double y, std::string_view movingItem);

// Checks that item may stand on lower: both on the same floor, lower is not the item, does not stand on the
// item at any depth, and neither hangs on a wall. InvalidValue with a plain sentence, NotFound for an unknown
// id.
Status checkStack(const Floor& floor, std::string_view item, std::string_view lower);

// The items that stand on an item, directly or through others, lowest first.
std::vector<Id> itemsStackedOn(const Floor& floor, std::string_view item);

// The top of an item, its z plus its height.
double itemTop(const Item& item);

}
