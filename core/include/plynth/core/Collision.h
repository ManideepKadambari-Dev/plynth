#pragma once

#include "BuildGeometry.h"
#include "Plan.h"

#include <optional>
#include <string_view>
#include <vector>

namespace plynth {

// Collision shapes for Unreal (DESIGN.md sections 7.13 step 11 and 7.16), worked out in core from the same
// shapes the build makes, so the Blender side only turns each piece into a closed convex mesh named
// UCX_<object>_<NN>. Unreal needs every collision mesh to be convex, so each piece is a convex outline in
// world X and Y raised from bottom to top (a vertical prism). Pieces may overlap; collision does not mind.
//
// What gets collision:
//   walls       per wall, its band at the wall's height, cut along the wall into pieces at every door and
//               plain opening; above such an opening a piece from its top (sill plus height) to the wall top,
//               below a plain opening with a sill a piece from the floor to the sill. Windows do not cut the
//               pieces, so the wall stays closed where the glass is. A curved wall gives one piece per arc
//               segment (arcSegmentCount). An end that meets another wall reaches half that wall's thickness
//               further, so corners have no gap.
//   floors      per room, every convex piece of its floor polygons, kCollisionSlabThickness thick below the
//               floor level
//   ceilings    the same pieces, kCollisionSlabThickness thick above the ceiling
//   thresholds  per doorway with sill 0, the convex pieces of its threshold, as thick as a floor slab
//   door leaves and window fills get none, so doorways can be walked through
//   furniture   one convex hull of each item's asset, made by the Blender build (core does not know the
//               asset's mesh)

inline constexpr double kCollisionSlabThickness = 0.1; // metres

// The built object a set of pieces belongs to. The names are the objects' plynth_kind custom property, and
// id is their plynth_id: the floor id for walls and thresholds, the room id for floors and ceilings.
enum class CollisionTargetKind {
    Walls,      // "walls"
    Floor,      // "floor"
    Ceiling,    // "ceiling"
    Thresholds, // "thresholds"
};

std::string_view collisionTargetKindName(CollisionTargetKind kind);
std::optional<CollisionTargetKind> collisionTargetKindFromName(std::string_view name);

// One convex prism. outline is anticlockwise seen from above, at least 3 points, the first not repeated.
// bottom and top are world Z, top above bottom.
struct CollisionPiece {
    std::vector<Vec2> outline;
    double bottom = 0.0;
    double top = 0.0;

    bool operator==(const CollisionPiece&) const = default;
};

// The pieces of one built object, in the order they are named (UCX_<object>_00, _01, ...).
struct CollisionShape {
    CollisionTargetKind kind = CollisionTargetKind::Walls;
    Id id;
    std::vector<CollisionPiece> pieces;

    bool operator==(const CollisionShape&) const = default;
};

// The collision of one floor by the rules above, from the floor and what the build makes of it: walls first
// (one shape for the floor, pieces in wall order), then each room's floor and ceiling in room order, then the
// thresholds (one shape, in opening order). Objects with no pieces are left out.
std::vector<CollisionShape> floorCollision(const Floor& floor, const FloorBuild& build);

// Cuts a polygon with holes into convex pieces: horizontal strips at the Y of every point (each strip piece is a
// trapezoid, so convex), then neighbours in the same column merged while the result stays convex. Each piece
// anticlockwise. Empty for an empty or degenerate polygon.
std::vector<std::vector<Vec2>> convexPieces(const WorldPolygon& polygon);

// True when the ring is convex and anticlockwise (collinear points allowed), with at least 3 points.
bool isConvexRing(const std::vector<Vec2>& ring);

}
