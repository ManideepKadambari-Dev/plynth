#pragma once

#include "plynth/core/Items.h"
#include "plynth/core/Plan.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/Stacking.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace plynth::detail {

// Keeps a copy of an item as it was before a command first changed it, once for each item.
inline void keepOld(std::vector<Item>& saved, const Item& item)
{
    for (const Item& s : saved) {
        if (s.id == item.id) return;
    }
    saved.push_back(item);
}

// Puts saved items back as they were.
inline void restoreItems(Floor& floor, const std::vector<Item>& saved)
{
    for (const Item& s : saved) {
        if (Item* item = findItem(floor, s.id)) *item = s;
    }
}

// Brings the items standing on an item along after it changed from before to how it is now: they keep their
// place on it, moved and turned with it, each stands on the top of the one below, and their rooms follow where
// they are. Each one is saved before it changes.
inline void carryStack(Floor& floor, const Item& before, std::vector<Item>& saved)
{
    const Item* now = findItem(floor, before.id);
    if (!now) return;
    const double turn = now->rotation - before.rotation;
    const bool moved = now->x != before.x || now->y != before.y || turn != 0.0;
    const double r = turn * std::numbers::pi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    for (const Id& id : itemsStackedOn(floor, before.id)) {
        Item* upper = findItem(floor, id);
        const Item* below = upper && upper->on ? findItem(floor, *upper->on) : nullptr;
        if (!below) continue;
        Item changed = *upper;
        if (moved) {
            const double dx = upper->x - before.x;
            const double dy = upper->y - before.y;
            changed.x = now->x + dx * c - dy * s;
            changed.y = now->y + dx * s + dy * c;
            changed.rotation = normalizeRotation(upper->rotation + turn);
            const Id room = roomAt(floor, {changed.x, changed.y});
            if (room.empty()) {
                changed.room.reset();
            } else {
                changed.room = room;
            }
        }
        changed.z = itemTop(*below);
        if (changed == *upper) continue;
        keepOld(saved, *upper);
        *upper = std::move(changed);
    }
}

// Lets the items standing right on an item down to where that item stood, for when it is removed or hung on a
// wall: onto the item it stood on, or onto the floor. What stands on them comes along. lower is the item as it
// was. Each changed item is saved first.
inline void releaseStack(Floor& floor, const Item& lower, std::vector<Item>& saved)
{
    const Item* base = lower.on ? findItem(floor, *lower.on) : nullptr;
    const double z = base ? itemTop(*base) : 0.0;
    for (std::size_t i = 0; i < floor.items.size(); ++i) {
        Item& upper = floor.items[i];
        if (!upper.on || *upper.on != lower.id) continue;
        const Item before = upper;
        keepOld(saved, upper);
        if (base) {
            upper.on = lower.on;
        } else {
            upper.on.reset();
        }
        upper.z = z;
        carryStack(floor, before, saved);
    }
}

// Adds to saved every item of more that saved does not hold yet, for folding one command into an earlier one.
inline void mergeSaved(std::vector<Item>& saved, const std::vector<Item>& more)
{
    for (const Item& item : more) keepOld(saved, item);
}

}
