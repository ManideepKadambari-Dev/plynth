#pragma once

#include "Geometry.h"
#include "Plan.h"
#include "Stairs.h"
#include "Status.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// What the Blender build makes, worked out in core from the plan (DESIGN.md section 7.13 steps 1 to 6; slabs
// and floor holes in Storeys.h, stairs in Stairs.h, roofs in Roofs.h).
// Every shape comes from the functions the app draws with (WallGeometry.h, Openings.h, OpeningShapes.h,
// Rooms.h, Items.h), so the plan view and the built building agree, and the Blender side only turns finished
// outlines into meshes.
//
// World coordinates are Blender's, in metres: X = plan x, Y = -plan y, Z up from the first floor's level.
// Rings are seen from above (+Z): outer rings anticlockwise, holes clockwise, the first point not repeated,
// no two points in a row closer than 1e-6. Turning y into -y reverses the direction of a polygon, so a plan
// polygon with a positive signedArea has its points reversed to become an anticlockwise outer ring, and every
// ring here has a positive signedArea (outer) or a negative one (hole) when measured on its world points.
// Curves are cut with arcSegmentCount, like everywhere else.

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const Vec3&) const = default;
};

// How far an opening's cutter reaches past each wall face, and below the floor or above the wall top when
// the opening touches them, so no cut face lies exactly on a wall face.
inline constexpr double kBuildCutMargin = 0.05;

// How far beside a wall face core looks for the room that face shows into.
inline constexpr double kBuildFaceProbe = 0.02;

// The width of the frame that door and window fills put inside the opening's outline.
inline constexpr double kOpeningFrameWidth = 0.05;

// A plan position in world X and Y.
Vec2 planToWorld(Vec2 plan);

// The level of each floor above the first floor's, in plan order: the sum of floorRise (ceiling height plus
// slab thickness, Storeys.h) of the floors before it.
std::vector<double> floorElevations(const Plan& plan);

// What one side face of a wall solid shows. The face stands on one edge of a ring.
struct WallEdge {
    Id wall;                      // the wall whose band holds the edge; for an end face, the nearest wall
    std::optional<WallSide> side; // the wall face the edge belongs to; empty for an end face across a band
    Id room;                      // the room the face looks into; empty when it looks into no room (outside)

    bool operator==(const WallEdge&) const = default;
};

// A closed ring of a wall solid. edges[i] runs from points[i] to points[i + 1], the last one back to the
// first, so both lists have the same length. The solid lies to the left of each edge as seen from above,
// so the face's outward direction is to the right of the edge.
struct WallRing {
    std::vector<Vec2> points; // world X and Y
    std::vector<WallEdge> edges;

    bool operator==(const WallRing&) const = default;
};

// A piece of a floor's walls that has one height: an outline with holes, raised from the floor's level.
struct WallSolid {
    double height = 0.0; // above the floor's level
    WallRing outline;
    std::vector<WallRing> holes;

    bool operator==(const WallSolid&) const = default;
};

// The walls of a floor as solids that never overlap. Walls are grouped by wallHeight. The solid of one height
// is the union of its walls' bands (wallOutline) minus the bands of every taller wall, so a half wall stops at
// the face of the full wall it meets and corners join without overlapping faces. One solid per outline the
// union gives, with the holes inside it (usually the rooms the walls close). Listed tallest first, then by
// the smallest wall id among their edges.
// Edge rules: an edge belongs to wall w and side s when its middle lies on that face of w's band (within
// 1e-3 m of half the thickness from w's centre line, on side s) and it runs along the wall (within
// kInLineAngle of its tangent there); otherwise it is an end face of the nearest wall. room is the room whose
// floor area (roomShape) holds the point kBuildFaceProbe to the right of the edge's middle.
std::vector<WallSolid> wallSolids(const Floor& floor);

// A polygon with holes in world X and Y.
struct WorldPolygon {
    std::vector<Vec2> outline;
    std::vector<std::vector<Vec2>> holes;

    bool operator==(const WorldPolygon&) const = default;
};

// The solid an opening cuts out of its wall: two rings of the same length, one beyond each wall face.
// Point i of both is the same point of the opening's outline. On a curved wall the outline gets extra points
// where the wall's curve is cut into segments, so the cut follows the curve.
struct OpeningCutter {
    std::vector<Vec3> right; // kBuildCutMargin beyond the wall's right face
    std::vector<Vec3> left;  // kBuildCutMargin beyond the wall's left face

    bool operator==(const OpeningCutter&) const = default;
};

// One opening as the build makes it.
// The opening's own box (OpeningShapes.h): u along the wall from -width / 2 at the Start jamb to width / 2,
// v up from 0 at the sill. A point (u, v, w) of the box, with w across the wall toward its right side, is at
// world centre + tangent * u + right * w in X and Y and at Z = floor level + sill + v. On a curved wall this
// is the plane that touches the wall's centre line at the opening's centre.
struct OpeningBuild {
    Id id;
    Id wall;
    OpeningType type = OpeningType::Door;
    std::string shape;
    double width = 0.0;
    double height = 0.0;
    double sill = 0.0;
    HingeEnd hinge = HingeEnd::Start;
    WallSide swing = WallSide::Right;
    std::optional<std::string> frame; // a catalog asset id; not fitted yet (see the plan for milestone 8)
    Vec2 centre;          // world X and Y on the wall's true centre line at the opening's offset
    Vec2 tangent;         // unit world direction along the wall toward its to point, at the centre
    Vec2 right;           // unit world direction of the wall's right side, at the centre
    double thickness = 0.0;
    bool curved = false;  // the wall is curved
    std::vector<Vec2> outline;      // the shape's outline in the opening's own box, (u, v)
    // outline made smaller by kOpeningFrameWidth on every side, except that for doors and plain openings with
    // sill 0 the bottom is kept at v = 0, so a door leaf reaches the floor. Empty when nothing is left.
    std::vector<Vec2> innerOutline;
    OpeningCutter cutter;
    // The cut through the wall band at floor level (OpeningPlacement::footprint) in world X and Y, when the
    // sill is 0, so the floor carries on through the doorway. Empty when the sill is above 0.
    std::vector<Vec2> threshold;

    bool operator==(const OpeningBuild&) const = default;
};

struct OpeningBuildResult {
    Status status; // what checkOpeningFit says; an opening that does not fit is not built
    OpeningBuild opening;
};

// Works out how one opening is built on a floor at a level.
OpeningBuildResult openingBuild(const Floor& floor, const Opening& opening, double elevation);

// One room's floor and ceiling.
struct RoomBuild {
    Id id;
    std::optional<std::string> type;
    double area = 0.0;     // RoomShape::area, square metres
    double floorZ = 0.0;   // the floor's level
    double ceilingZ = 0.0; // the floor's level plus its ceiling height
    std::vector<WorldPolygon> polygons; // RoomShape::floorArea minus floorHoles: the floor face
    std::vector<WorldPolygon> ceilingPolygons; // RoomShape::floorArea minus ceilingHoles: the ceiling face

    bool operator==(const RoomBuild&) const = default;
};

// Works out a room's floor and ceiling. Empty when the room's walls do not close (RoomShape::closed false).
// Without holes, ceilingPolygons equal polygons.
std::optional<RoomBuild> roomBuild(const Floor& floor, const Room& room, double elevation);

// The same with stair holes: floorHoles are the footprints of stairs arriving from the floor below,
// ceilingHoles those of stairs that climb from this floor (StairBuild::hole), in world coordinates. area stays
// RoomShape::area.
std::optional<RoomBuild> roomBuild(const Floor& floor, const Room& room, double elevation,
                                   const std::vector<WorldPolygon>& floorHoles,
                                   const std::vector<WorldPolygon>& ceilingHoles);

// Where an item's asset goes in the world, by the mapping in Items.h: location (x, -y, level + z) and a turn
// about Z of -rotation, in radians. The asset's pivot (bottom centre) goes to location.
struct ItemPlacement {
    Vec3 location;
    double rotationZ = 0.0;

    bool operator==(const ItemPlacement&) const = default;
};

// Works out where an item goes on a floor at a level.
ItemPlacement itemPlacement(const Item& item, double elevation);

// One placed item as the build makes it.
struct ItemBuild {
    Id id;
    std::string asset;
    Vec3 location;
    double rotationZ = 0.0;
    ItemSize size;
    std::optional<Id> wall;

    bool operator==(const ItemBuild&) const = default;
};

// One piece of a stair in world coordinates: the outline anticlockwise, bottom and top world Z.
struct StairStepBuild {
    StairPart part = StairPart::Step;
    std::vector<Vec2> outline;
    double bottom = 0.0;
    double top = 0.0;

    bool operator==(const StairStepBuild&) const = default;
};

// One stair as the build makes it.
struct StairBuild {
    Id id;
    std::string type;
    Id toFloor;
    double rise = 0.0;
    int riserCount = 0;
    double riser = 0.0;
    std::vector<StairStepBuild> steps;
    WorldPolygon hole; // the footprint, cut from the slab, this floor's ceilings and the next floor's floors

    bool operator==(const StairBuild&) const = default;
};

// Turns a stair layout whose status is ok into world coordinates at a floor level.
StairBuild stairBuild(const StairLayout& layout, double elevation);

// What one side face of a slab is, for its material. The ring's kind does not say it: a courtyard is a hole
// ring of edge faces, and a well that reaches the boundary is part of the outline.
enum class SlabSide {
    Edge, // "edge": the outside band
    Well, // "well": the side of a stair well
};

std::string_view slabSideName(SlabSide side);

// The sides of one slab polygon. outline[i] is the face on the edge from point i of the polygon's outline to
// the next, the last back to the first, and holes[h][i] the same on its hole h.
struct SlabPolygonSides {
    std::vector<SlabSide> outline;
    std::vector<std::vector<SlabSide>> holes;

    bool operator==(const SlabPolygonSides&) const = default;
};

// The slab above one floor (Storeys.h). Side faces stand on every ring of polygons from bottom to top, each
// what sides says. Bottom faces are made only for undersides, top faces only for tops.
struct SlabBuild {
    Id floor;
    double bottom = 0.0; // world Z: the floor's level plus its ceiling height
    double top = 0.0;    // world Z: the next floor's level
    std::vector<WorldPolygon> polygons;
    std::vector<WorldPolygon> undersides;
    std::vector<SlabPolygonSides> sides; // one per entry of polygons, in the same order
    std::vector<WorldPolygon> tops;      // the ledges, at top, facing up, with the edge material

    bool operator==(const SlabBuild&) const = default;
};

// What a roof face is, for its material.
enum class RoofPart {
    Covering,  // "covering": the top surface
    Underside, // "underside": below the roof, seen under the eaves
    Edge,      // "edge": the faces round the roof's thickness (fascia)
    Gable,     // "gable": the vertical fill between the slab and the roof's underside at gable ends
    Parapet,   // "parapet": every face of a flat roof's parapet
    Glazing,   // "glazing": the vertical glass of a sawtooth bay
};

std::string_view roofPartName(RoofPart part);
std::optional<RoofPart> roofPartFromName(std::string_view name);

// One planar face of a roof in world coordinates. outline is anticlockwise seen from the side the face looks
// toward, holes the other way. No two points in a row closer than 1e-6.
struct RoofFace {
    RoofPart part = RoofPart::Covering;
    std::vector<Vec3> outline;
    std::vector<std::vector<Vec3>> holes;

    bool operator==(const RoofFace&) const = default;
};

// One roof section as the build makes it.
struct RoofBuild {
    Id floor;
    int index = 0;      // RoofArea::index
    Id section;         // the RoofSection entry that applies; empty for the default
    std::string type;   // the type built (the fallback when the chosen one did not fit)
    std::vector<RoofFace> faces;

    bool operator==(const RoofBuild&) const = default;
};

// Everything the build makes for one floor. Openings, rooms and items that cannot be built are left out; the
// build plan (BuildJob.h) says why.
struct FloorBuild {
    Id id;
    double elevation = 0.0;
    double ceilingHeight = 0.0;
    std::vector<WallSolid> wallSolids;
    std::vector<OpeningBuild> openings; // in the floor's opening order
    std::vector<RoomBuild> rooms;       // in the floor's room order
    std::vector<ItemBuild> items;       // in the floor's item order
    SlabBuild slab;                     // the slab above the floor
    std::vector<StairBuild> stairs;     // stairs that climb from this floor and can be built, in stair order
    std::vector<RoofBuild> roofs;       // the floor's roof sections, in index order
    // The thickness of the slab this floor stands on: the Floor::slabThickness of the floor below it in the plan.
    // Empty for the lowest floor. Not in the build job.
    std::optional<double> slabBelow;

    bool operator==(const FloorBuild&) const = default;
};

}
