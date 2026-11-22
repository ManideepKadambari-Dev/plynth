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
// world X and Y raised from bottom to top (a vertical prism), and what slopes is a hull: points whose convex
// hull the Blender side makes. Pieces and hulls may overlap; collision does not mind.
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
//   ceilings    per room, every convex piece of its ceilingPolygons, kCollisionSlabThickness thick above the
//               ceiling, so a stair hole stays open
//   thresholds  per doorway with sill 0, the convex pieces of its threshold, as thick as a floor slab
//   slabs       every convex piece of each of SlabBuild::polygons, from its bottom to its top
//   stairs      per stair, one piece per step, landing and column, in the order of StairBuild::steps: its
//               outline from its bottom to its top
//   roofs       per roof section, one hull per convex piece of each covering face: the face is cut with
//               convexPieces as seen from above, and a hull is the piece's points on the face's plane followed
//               by the same points kRoofThickness (Roofs.h) lower. For a parapet, pieces: the convex pieces of
//               each parapet face that looks up, from the lowest Z of the section's parapet faces to that
//               face's Z. Gables and glazing get none.
//   door leaves and window fills get none, so doorways can be walked through
//   furniture   one convex hull of each item's asset, made by the Blender build (core does not know the
//               asset's mesh)

inline constexpr double kCollisionSlabThickness = 0.1; // metres

// The built object a set of pieces belongs to. The names are the objects' plynth_kind custom property, and
// id is their plynth_id: the floor id for walls, thresholds, slabs and roofs, the room id for floors and
// ceilings, the stair id for stairs. A floor's roofs are told apart by plynth_roof_index (CollisionShape::index).
enum class CollisionTargetKind {
    Walls,      // "walls"
    Floor,      // "floor"
    Ceiling,    // "ceiling"
    Thresholds, // "thresholds"
    Slab,       // "slab"
    Stair,      // "stair"
    Roof,       // "roof"
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

// The collision of one built object, in the order it is named (UCX_<object>_00, _01, ...): the pieces, then
// the hulls, numbered on from the last piece.
struct CollisionShape {
    CollisionTargetKind kind = CollisionTargetKind::Walls;
    Id id;
    std::vector<CollisionPiece> pieces;
    int index = 0;                        // RoofBuild::index for a roof; 0 otherwise
    std::vector<std::vector<Vec3>> hulls; // world points, at least 4 each and not all in one plane; roofs only

    bool operator==(const CollisionShape&) const = default;
};

// The collision of one floor by the rules above, from the floor and what the build makes of it: walls first
// (one shape for the floor, pieces in wall order), then each room's floor and ceiling in room order, then the
// thresholds (one shape, in opening order), then the slab, then each stair in stair order, then each roof in
// index order. Objects with no pieces and no hulls are left out.
std::vector<CollisionShape> floorCollision(const Floor& floor, const FloorBuild& build);

// Cuts a polygon with holes into convex pieces: horizontal strips at the Y of every point (each strip piece is a
// trapezoid, so convex), then neighbours in the same column merged while the result stays convex. Each piece
// anticlockwise. Empty for an empty or degenerate polygon.
std::vector<std::vector<Vec2>> convexPieces(const WorldPolygon& polygon);

// True when the ring is convex and anticlockwise (collinear points allowed), with at least 3 points.
bool isConvexRing(const std::vector<Vec2>& ring);

}
