#pragma once

#include "Choices.h"
#include "FinishSettings.h"
#include "MaterialDefaults.h"
#include "Plan.h"

#include <string>
#include <vector>

namespace plynth {

// Variations (DESIGN.md section 7.15): a seed changes furniture and materials of a copy of the plan, never the
// plan the user marked. Seed 0 is the plan as marked. Another seed, with its switch on:
// - Furniture: an asset item may become another choice that shares a category with the marked asset, with a
//   width from kVariationMinWidth to kVariationMaxWidth and a depth from kVariationMinDepth to
//   kVariationMaxDepth of the marked size, and no item problem (checkItem) the marked one did not have. Its
//   id, centre, rotation, z and room stay; its size becomes the new asset's. A wall item is hung flush on its
//   wall face again. Generator items, items with an empty asset and items whose asset is not a choice are
//   left alone.
// - Materials: each room's floor among materials of group "floor", each room's wall faces among group "wall",
//   and one choice for every outside wall face among group "exterior". A material the user marked that is not
//   in that group is left alone. Ceilings, stairs, roofs, trim and railings are left alone. A choice is
//   written into the copy as a plan override (Room::floorMaterial, Room::wallMaterial,
//   Wall::materialOutside), never into the defaults.
// Every choice is a seededChoice keyed by what is chosen, so the order of the choices does not matter.

inline constexpr double kVariationMinWidth = 0.75;
inline constexpr double kVariationMaxWidth = 1.05;
inline constexpr double kVariationMinDepth = 0.9;
inline constexpr double kVariationMaxDepth = 1.05;

inline constexpr std::string_view kFloorMaterialGroup = "floor";
inline constexpr std::string_view kWallMaterialGroup = "wall";
inline constexpr std::string_view kExteriorMaterialGroup = "exterior";

// What a change touched. Names: "item", "room_floor", "room_wall", "wall_inside", "outside".
enum class VariationTarget { Item, RoomFloor, RoomWall, WallInside, Outside };

std::string_view variationTargetName(VariationTarget target);

// One change: the item's asset or the material of a room or wall, from what the plan gave to what the seed
// chose. from is the asset or the material in effect before (the default when the plan names none). For
// Outside, object is empty: one choice covers every outside face.
struct VariationChange {
    VariationTarget target = VariationTarget::Item;
    Id object;
    std::string from;
    std::string to;

    bool operator==(const VariationChange&) const = default;
};

struct VariationResult {
    Plan plan;
    std::vector<VariationChange> changes; // floors in plan order, items before rooms, then the outside choice

    bool operator==(const VariationResult&) const = default;
};

// The varied copy of a plan and what changed. With seed 0, or both switches off, or no candidates, the copy
// equals the plan and nothing changed.
VariationResult variedPlan(const Plan& plan, const MaterialDefaults& defaults, const VariationSettings& settings,
                           const BuildChoices& choices);

}
