#include "plynth/core/Variation.h"

#include "plynth/core/Items.h"
#include "plynth/core/SeededChoice.h"

#include <algorithm>
#include <set>
#include <utility>

namespace plynth {

namespace {

// Says whether a list of names holds one.
bool holds(const std::vector<std::string>& names, std::string_view name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

// The asset choice with an id, or null.
const AssetChoice* findChoice(const BuildChoices& choices, std::string_view id)
{
    for (const AssetChoice& a : choices.assets) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// The problems of an item as (kind, other) pairs, for comparing before and after a swap.
std::set<std::pair<int, std::string>> problemKeys(const Floor& floor, const Item& item)
{
    std::set<std::pair<int, std::string>> out;
    for (const ItemProblem& p : checkItem(floor, item)) out.insert({static_cast<int>(p.kind), p.other});
    return out;
}

// The item with another asset of a size, hung flush on its wall again when it is a wall item.
std::optional<Item> swapped(const Floor& floor, const Item& item, const AssetChoice& to)
{
    Item out = item;
    out.asset = to.id;
    out.size = to.size;
    if (item.wall) {
        const WallItemSpot spot = wallItemSpot(floor, item);
        const Wall* wall = findWall(floor, *item.wall);
        if (!spot.valid || !wall) return std::nullopt;
        const ItemPose pose = wallItemPose(floor, *wall, spot.side, spot.offset, to.size);
        if (!pose.status.ok()) return std::nullopt;
        out.x = pose.position.x;
        out.y = pose.position.y;
    }
    return out;
}

// Varies the asset items of a floor.
void varyFurniture(Floor& floor, std::uint32_t seed, const BuildChoices& choices, std::vector<VariationChange>& changes)
{
    for (std::size_t i = 0; i < floor.items.size(); ++i) {
        const Item item = floor.items[i];
        if (isGeneratorItem(item) || item.asset.empty()) continue;
        const AssetChoice* marked = findChoice(choices, item.asset);
        if (!marked) continue;
        const auto before = problemKeys(floor, item);
        std::vector<std::string> ids;
        std::vector<Item> options;
        for (const AssetChoice& c : choices.assets) {
            const bool sharesCategory = std::any_of(c.categories.begin(), c.categories.end(),
                                                    [&](const std::string& cat) { return holds(marked->categories, cat); });
            if (!sharesCategory) continue;
            if (c.id != item.asset) {
                const double w = c.size.width / item.size.width;
                const double d = c.size.depth / item.size.depth;
                if (w < kVariationMinWidth - 1e-9 || w > kVariationMaxWidth + 1e-9) continue;
                if (d < kVariationMinDepth - 1e-9 || d > kVariationMaxDepth + 1e-9) continue;
            }
            const std::optional<Item> candidate = c.id == item.asset ? std::optional<Item>(item) : swapped(floor, item, c);
            if (!candidate) continue;
            const auto after = problemKeys(floor, *candidate);
            if (!std::includes(before.begin(), before.end(), after.begin(), after.end())) continue;
            ids.push_back(c.id);
            options.push_back(*candidate);
        }
        const auto pick = seededChoice(seed, floor.id + "/" + item.id + "/asset", ids);
        if (!pick || ids[*pick] == item.asset) continue;
        floor.items[i] = options[*pick];
        changes.push_back({VariationTarget::Item, item.id, item.asset, ids[*pick]});
    }
}

// The ids of the materials in a group, in id order.
std::vector<std::string> groupMaterials(const BuildChoices& choices, std::string_view group)
{
    std::vector<std::string> out;
    for (const MaterialChoice& m : choices.materials) {
        if (holds(m.groups, group)) out.push_back(m.id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Chooses a material for one surface. Empty when the marked material is outside the group or there is no
// candidate. current is the material in effect, marked says the plan names it.
std::optional<std::string> chooseMaterial(std::uint32_t seed, const std::string& key, const std::vector<std::string>& group,
                                          const std::optional<std::string>& marked)
{
    if (group.empty()) return std::nullopt;
    if (marked && !holds(group, *marked)) return std::nullopt;
    const auto pick = seededChoice(seed, key, group);
    return group[*pick];
}

// Varies the materials of the rooms and walls of a floor.
void varyMaterials(Floor& floor, const MaterialDefaults& defaults, std::uint32_t seed, const BuildChoices& choices,
                   std::vector<VariationChange>& changes)
{
    const std::vector<std::string> floors = groupMaterials(choices, kFloorMaterialGroup);
    const std::vector<std::string> walls = groupMaterials(choices, kWallMaterialGroup);
    for (Room& room : floor.rooms) {
        const std::string fromFloor = room.floorMaterial.value_or(defaults.floor.value_or(""));
        if (const auto to = chooseMaterial(seed, floor.id + "/" + room.id + "/floor", floors, room.floorMaterial)) {
            if (*to != fromFloor) {
                room.floorMaterial = *to;
                changes.push_back({VariationTarget::RoomFloor, room.id, fromFloor, *to});
            }
        }
        const std::string fromWall = room.wallMaterial.value_or(defaults.wallInside.value_or(""));
        if (const auto to = chooseMaterial(seed, floor.id + "/" + room.id + "/wall", walls, room.wallMaterial)) {
            if (*to != fromWall) {
                room.wallMaterial = *to;
                changes.push_back({VariationTarget::RoomWall, room.id, fromWall, *to});
            }
        }
    }
    for (Wall& wall : floor.walls) {
        if (!wall.materialInside || !holds(walls, *wall.materialInside)) continue;
        const auto to = chooseMaterial(seed, floor.id + "/" + wall.id + "/inside", walls, wall.materialInside);
        if (to && *to != *wall.materialInside) {
            changes.push_back({VariationTarget::WallInside, wall.id, *wall.materialInside, *to});
            wall.materialInside = *to;
        }
    }
}

}

// The name of a variation target.
std::string_view variationTargetName(VariationTarget target)
{
    switch (target) {
    case VariationTarget::Item: return "item";
    case VariationTarget::RoomFloor: return "room_floor";
    case VariationTarget::RoomWall: return "room_wall";
    case VariationTarget::WallInside: return "wall_inside";
    case VariationTarget::Outside: return "outside";
    }
    return "item";
}

// The varied copy of a plan.
VariationResult variedPlan(const Plan& plan, const MaterialDefaults& defaults, const VariationSettings& settings,
                           const BuildChoices& choices)
{
    VariationResult result;
    result.plan = plan;
    if (settings.seed == 0) return result;
    const std::uint32_t seed = settings.seed;
    for (Floor& floor : result.plan.floors) {
        if (settings.furniture) varyFurniture(floor, seed, choices, result.changes);
        if (settings.materials) varyMaterials(floor, defaults, seed, choices, result.changes);
    }
    if (settings.materials) {
        const std::vector<std::string> outside = groupMaterials(choices, kExteriorMaterialGroup);
        const std::string from = defaults.wallOutside.value_or("");
        if (const auto to = chooseMaterial(seed, "exterior", outside, std::nullopt)) {
            bool changed = false;
            for (Floor& floor : result.plan.floors) {
                for (Wall& wall : floor.walls) {
                    const std::string current = wall.materialOutside.value_or(from);
                    if (wall.materialOutside && !holds(outside, *wall.materialOutside)) continue;
                    if (current == *to) continue;
                    wall.materialOutside = *to;
                    changed = true;
                }
            }
            if (changed) result.changes.push_back({VariationTarget::Outside, {}, from, *to});
        }
    }
    return result;
}

}
