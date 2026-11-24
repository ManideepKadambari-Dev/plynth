#pragma once

#include "BuildGeometry.h"
#include "FinishSettings.h"
#include "Geometry.h"
#include "Plan.h"

#include <vector>

namespace plynth {

// Trim worked out in core (DESIGN.md sections 7.13 step 9 and 7.13a), so the Blender side only skins finished
// cross sections into meshes. World coordinates as in BuildGeometry.h.
//
// What gets trim:
//   skirting  along every wall face that looks into a room, at the floor, as one path per run of the room's
//             floor outline. Broken where a door or an opening reaches lower than the skirting's top (the
//             gap is the opening's width plus the casing height on each side when casings are on), and inside
//             the floor polygons of skipAtFloor (stairs arriving from below and stairs climbing from here).
//   cornice   along the same faces where the wall reaches the ceiling, at the ceiling. Broken where an opening
//             reaches higher than the cornice's bottom, at half walls and inside skipAtCeiling.
//   casing    round both faces of every door, along the outline without its bottom, following the shape and
//             the wall's curve.
//   sill      on both faces of every window whose outline has a straight bottom of at least kMinSillWidth,
//             reaching kSillOverhang past each jamb.
//   railing   not here: see StairRailings.h.
// A path is broken into separate sweeps where it is cut, never bridged.

inline constexpr double kMinSillWidth = 0.3;
inline constexpr double kSillOverhang = 0.05;

// A swept cross section: rings of world points, one per point of the path, each with the same count. closed
// says the last section joins the first. Section k point j is profile point j placed at path point k.
struct Sweep {
    std::vector<std::vector<Vec3>> sections;
    bool closed = false;

    bool operator==(const Sweep&) const = default;
};

// The trim of one kind on one floor. profileMissing says the style's profile is not registered and the square
// profile was used instead; the build reports it as trim_profile_missing.
struct TrimBuild {
    TrimKind kind = TrimKind::Skirting;
    std::vector<Sweep> sweeps;
    bool profileMissing = false;

    bool operator==(const TrimBuild&) const = default;
};

// The trim of a floor by the rules above, one entry per enabled kind that gives at least one sweep, in the
// order of TrimKind. build is what openingBuild, roomBuild and wallSolids give for the floor.
std::vector<TrimBuild> trimBuild(const Floor& floor, const FloorBuild& build, const TrimSettings& settings,
                                 const std::vector<WorldPolygon>& skipAtFloor,
                                 const std::vector<WorldPolygon>& skipAtCeiling);

// Sweeps a profile along a path of world points. Profile x is placed along the side direction (the path's
// direction crossed with up, so to the right of the path seen from up), profile y along up. At an inner point the
// section lies on the plane halfway between the two segments (a mitre), its side offset stretched so both faces
// stay parallel to the path. An open path's end sections stand square to the first and last segment. Points
// closer than 1e-6 are dropped. Empty when fewer than two distinct points remain.
Sweep sweepPath(const std::vector<Vec3>& path, bool closed, const std::vector<Vec2>& profile, Vec3 up);

}
