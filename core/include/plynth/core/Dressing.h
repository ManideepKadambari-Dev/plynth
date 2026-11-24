#pragma once

#include "Choices.h"
#include "Geometry.h"
#include "Plan.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Set dressing (DESIGN.md section 7.14): rules that add small props to a room, drawn from tagged catalog
// assets. Rules are registry entries: a built-in rule is one file in core/src/dressing/rules/ with a
// DressingRuleRegistrar at namespace scope, so adding one never means editing existing code.
//
// The built-in rules, in order:
// - books (10): props tagged "book" in rows on the supports of items of category "shelf", filling 40 to 80
//   percent of each support's width.
// - tabletop (20): 1 to 3 props tagged "tabletop" on the top support of items of category "table", "desk" or
//   "cabinet".
// - pictures (30): props tagged "wall_art", at most one on each wall face of the room with 1.5 m free and at
//   most three in the room, centred 1.5 m above the floor, clear of openings, wall items and furniture taller
//   than 1.2 m, facing into the room.
// A room gets at most kMaxDressingPerRoom props, and props never overlap one another. No tagged asset means no
// props and no problem. Generator items are never hosts.

inline constexpr std::size_t kMaxDressingPerRoom = 40;

// One prop. Positions are plan metres (x right, y down) of the prop's pivot (its bottom centre), z above the
// floor, rotation in degrees as Item::rotation. host is the item it stands on, or the wall it hangs on for a
// picture.
struct DressingPlacement {
    Id room;
    Id host;
    std::string rule;
    std::string asset;
    Vec2 position;
    double z = 0.0;
    double rotation = 0.0;
    ItemSize size;

    bool operator==(const DressingPlacement&) const = default;
};

// What a rule sees: the room, the assets to draw from (sorted by id), the props earlier rules placed in the room,
// and the seed.
struct DressingInput {
    const Floor& floor;
    const Room& room;
    const std::vector<AssetChoice>& assets;
    const std::vector<DressingPlacement>& placed;
    std::uint32_t seed = 0;
};

class DressingRule {
public:
    virtual ~DressingRule() = default;

    // Stable id, for example "books". Lower case letters, digits and "_".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Books on shelves".
    virtual std::string_view name() const = 0;
    // Rules run by this, then by id. Built-in rules use 10, 20, 30 and so on.
    virtual int order() const = 0;
    // Adds the rule's props for one room to out. It must not overlap the props in input.placed, and it must
    // give the same list for the same input, whatever the order of the assets.
    virtual void place(const DressingInput& input, std::vector<DressingPlacement>& out) const = 0;
};

// Adds a rule to the registry. Returns false and drops it when its id is taken or not a well formed lower case
// name.
bool registerDressingRule(std::unique_ptr<DressingRule> rule);

// Registers a rule while the program starts, from namespace scope in its file.
struct DressingRuleRegistrar {
    explicit DressingRuleRegistrar(std::unique_ptr<DressingRule> rule);
};

// The rule with this id. Null when there is none.
const DressingRule* findDressingRule(std::string_view id);

// Every registered rule, by order and then id.
std::vector<const DressingRule*> allDressingRules();

// Runs every rule on a room in turn. Each sees what the ones before it placed. Props that overlap one placed
// before them, and every prop past kMaxDressingPerRoom, are dropped. The assets are sorted by id first, so their
// order does not matter. Empty for a room whose walls do not close.
std::vector<DressingPlacement> dressRoom(const Floor& floor, const Room& room, const std::vector<AssetChoice>& assets,
                                         std::uint32_t seed);

// Says whether two props overlap: their boxes (footprint turned by rotation, from z to z plus height) share
// more than kItemOverlapArea of footprint and some height. Rules use it to keep clear of earlier props.
bool dressingOverlaps(const DressingPlacement& a, const DressingPlacement& b);

}
