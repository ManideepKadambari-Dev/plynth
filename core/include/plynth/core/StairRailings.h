#pragma once

#include "BuildGeometry.h"
#include "Collision.h"
#include "FinishSettings.h"
#include "Plan.h"
#include "Trim.h"

#include <string>
#include <vector>

namespace plynth {

// Stair railings and guards (milestone 15), worked out in core like the rest of the trim. World coordinates as in
// BuildGeometry.h. With the railing style on:
//   railing  along each open side of a stair, kStairSideOffset in from its edge: a rail at the railing height
//            above the steps, passing over the middle of each step and level across landings, and posts at
//            most kMaxPostSpacing apart. A side whose rail runs within kRailingWallGap of a wall's face gets
//            none. A spiral stair gets its outer side only.
//   guard    round the stair's hole on the floor above, at that floor's level, leaving the edge nearest the
//            arrival point open, with the same rail height and post spacing, and none along walls.
// Rails and posts use the railing style's profile, centred on the path. Every collision piece is a convex prism.

inline constexpr double kStairSideOffset = 0.04;
inline constexpr double kMaxPostSpacing = 1.0;
inline constexpr double kRailingWallGap = 0.15;

struct StairRailing {
    Id stair;
    Id floor;                  // the floor the object stands on: the stair's floor, or the floor above for a guard
    std::string kind;          // "railing" or "guard", also the object's plynth_kind
    std::vector<Sweep> sweeps; // the rails, then the posts (each post a sweep of two sections)
    std::vector<CollisionPiece> collision;
    bool profileMissing = false;

    bool operator==(const StairRailing&) const = default;
};

// The object name of a railing: SM_<floor>_<stair>_Railing or SM_<floor>_<stair>_Guard.
std::string railingObjectName(const StairRailing& railing);

// The railings and guards of every stair of a plan that can be built, in floor and stair order, each stair's
// railing before its guard. elevations are floorElevations of the plan. Empty when the railing style is off.
std::vector<StairRailing> stairRailings(const Plan& plan, const TrimSettings& settings, const std::vector<double>& elevations);

}
