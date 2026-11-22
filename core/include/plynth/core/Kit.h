#pragma once

#include "BuildGeometry.h"
#include "BuildJob.h"
#include "Collision.h"
#include "Plan.h"
#include "Project.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The modular kit (DESIGN.md sections 7.2 and 7.16): the unique wall, corner and opening pieces of the
// building's walls as separate reusable meshes, plus a layout file saying where each one goes. Worked out in
// core from the same wall solids and openings the build makes (BuildGeometry.h), so the Blender side only turns
// finished outlines into meshes, as for the build. An optional extra output of any export mode, switched on in
// the project's export options (ExportGroups.h). Written into export/ next to the group files:
//   export/<Stem>_Kit.fbx           every unique piece once, each at its own origin, with its UCX_ collision
//   export/<Stem>_Kit_layout.json   where each piece goes (schema/kit-layout.schema.json), written by core
// Stem is exportFileStem of the project name. Floors, ceilings, thresholds and furniture are not in the kit.
//
// The kit grid is fixed per project and is the project's grid size (ProjectSettings::gridSize, the wall snap
// grid), so every floor is cut on the same grid. kitGridSize is the only place that reads it.
//
// How the walls of one floor are cut (lengths in metres along a wall's centre line, measured from its from
// point):
//   1. Junctions. For every wall, the parts of its centre line where its band (wallBand) overlaps the band of
//      another wall of the floor, each widened to the whole length of that overlap seen along the wall, are its
//      junction parts. A wall end that meets nothing has none: its end face is a real end.
//   2. Stations on a straight wall: its two ends, the ends of its junction parts, and the grid stations outside
//      those parts. Grid stations are whole multiples of the grid counted from the wall's lower end (the smaller
//      world X, for equal X the smaller world Y), whichever way it was drawn. A grid station strictly inside an
//      opening's width widened by kKitOpeningMargin on each side is dropped (an opening 0.9 wide centred at 2.0
//      drops 2.0 and keeps 1.5 and 2.5 on a 0.5 grid), and so is one closer than kKitMinPieceLength to a wall
//      end, to a junction part's end or to the grid station before it, so an opening is never cut and no sliver
//      is made.
//   3. Wall pieces: between two stations outside the junction parts, the floor's wall solids cut by the two
//      lines across the wall at those stations. Kind Wall, or Opening when an opening's cut reaches into it.
//   4. A curved wall is one piece from the end of its first junction part to the start of its last (kind
//      Curved, DESIGN.md section 7.2 "Curved walls are unique pieces"), with its openings.
//   5. Corner pieces: what is left of the wall solids once every wall and curved piece is taken out, split into
//      parts that do not touch. Each is one piece of kind Corner (an L corner, a T or a cross junction, or a
//      wall too short to have a middle).
//   6. Faces. A side face on a cut line has kit cut set, no side and no room; it is not bevelled, so pieces meet
//      without a groove. Every other face keeps the wall, side, room, material and slot of the build's face it
//      lies on, and the tops keep the solid's top slot.
//   7. Openings. A piece holds every opening whose cut reaches into its area, with the cutter in the piece's
//      frame; thresholds are left out (they belong to the floor). An opening's door or window fill goes with the
//      one piece that holds the opening's centre.
//   8. Frames a piece may take. In every one the origin is at the floor's level, Z is up and Y is X turned a
//      quarter turn anticlockwise seen from above, so a placement is a turn and a move, never a mirror.
//      Wall, Opening and Curved pieces have two. The found frame: the origin is on the wall's centre line at
//      the piece's first station, X along the wall toward its to point (on a curved wall, along the chord from
//      the first to the last station). Its half turn: that frame turned half a turn about the middle of the
//      piece's extent along X.
//      A Corner has one for each wall leaving its point, in wall id order: the origin is that point, X along
//      the wall as it leaves. The point is the plan point with walls that lies in or on the piece, the one
//      nearest the piece's centroid when there are several. With no such point the one frame is the centroid
//      with world X.
//      Rule 9 picks the frame, so a piece's origin is not always at its first station and its X does not
//      always point toward the wall's to point. The placement's location and rotationZ are the frame picked.
//   9. Same pieces. Pieces are looked at in the order found: floors in plan order; on a floor, walls in plan
//      order and along each from its from point, then corners by their lowest wall id and then by position.
//      - A piece that, in any of its frames, is the same as a piece made before is a placement of that piece,
//        in the first of its frames that is.
//      - Otherwise it is a new piece: it is written out as text in every one of its frames, and the frame
//        whose text sorts first is the one stored, hashed, named and placed. So equal pieces get one shape
//        and one name, whichever wall or direction finds them.
//      The same means equal kind and, within kKitTolerance, equal length, height, thickness, solid heights and
//      points; equal slot, material and kit cut of every face and top, and whether a face has a side (not
//      which wall or room); for each opening equal type, shape, curved, width, height, sill, thickness,
//      centre, cutter, outline and fill, the materials and slots of its parts, and the hinge and swing as
//      they lie in the piece's frame. The text holds the same things in a fixed order, with lengths in whole
//      millimetres (snapped to a tenth of a micrometre first, then rounded half up). Pieces that differ by no
//      more than the tolerance share the shape and name of the one found first.
//  10. Names. id is <Kind>_<L>x<H>x<T>_<hash>: L the size along the stored X, H the tallest solid's height, T
//      the size along the stored Y, all in whole centimetres, rounded; hash the first 6 of the 16 hex digits
//      of the 64 bit FNV-1a hash of the stored frame's text, so a piece keeps its name across exports while it
//      stays the same. An id another piece already has is hashed again with "#1", "#2" and so on after the
//      text. The mesh is SM_Kit_<id>, its collision UCX_SM_Kit_<id>_<NN>.
//  11. Collision. The rule for walls of Collision.h applied to the piece: convex prisms of its area, cut at doors
//      and plain openings with pieces above and below them, windows closed. In the piece's frame.
// UVs (made by the Blender side): side faces u along the face's own direction measured from the piece's
// origin, v up from 0; tops (X, Y) in the piece's frame; cut faces of openings projected as in the build.

inline constexpr double kKitOpeningMargin = 0.05;   // metres beside an opening where no grid station goes
inline constexpr double kKitMinPieceLength = 0.05;  // metres; a grid station this near another is dropped
inline constexpr double kKitTolerance = 0.001;      // metres
inline constexpr std::string_view kKitFileSuffix = "_Kit.fbx";
inline constexpr std::string_view kKitLayoutSuffix = "_Kit_layout.json";
inline constexpr std::string_view kKitLayoutFormat = "plynth-kit-layout";
inline constexpr int kKitLayoutVersion = 1;
inline constexpr std::string_view kKitMeshPrefix = "SM_Kit_";

// The grid the kit cuts a floor's walls on: the project's grid size, the same for every floor.
double kitGridSize(const Project& project, const Floor& floor);

enum class KitPieceKind {
    Wall,    // "wall"
    Corner,  // "corner"
    Opening, // "opening"
    Curved,  // "curved"
};

std::string_view kitPieceKindName(KitPieceKind kind);
std::optional<KitPieceKind> kitPieceKindFromName(std::string_view name);

// One side face of a kit solid: the build's face it lies on, with its material and slot, or a kit cut.
struct KitEdge {
    WallEdge edge;        // side and room empty for a kit cut and for an end face
    std::string material; // catalog id; empty for the neutral look
    std::string slot;     // a slot of the build (MaterialSlots.h)
    bool cut = false;     // the face lies on a cut line between two pieces

    bool operator==(const KitEdge&) const = default;
};

// A ring in the piece's frame, with the rules of WallRing.
struct KitRing {
    std::vector<Vec2> points;
    std::vector<KitEdge> edges;

    bool operator==(const KitRing&) const = default;
};

// A part of a piece with one height, from z 0 in the piece's frame.
struct KitSolid {
    double height = 0.0;
    std::string topMaterial;
    std::string topSlot;
    KitRing outline;
    std::vector<KitRing> holes;

    bool operator==(const KitSolid&) const = default;
};

// An opening a piece holds, in the piece's frame (centre, tangent, right and cutter turned and moved; Z from the
// floor's level). threshold is always empty.
struct KitOpening {
    OpeningBuild opening;
    OpeningMaterialIds materials;
    bool fill = false; // this piece gets the opening's door or window

    bool operator==(const KitOpening&) const = default;
};

struct KitPiece {
    std::string id;   // see rule 10
    std::string mesh; // kKitMeshPrefix + id
    KitPieceKind kind = KitPieceKind::Wall;
    double length = 0.0;    // along X
    double height = 0.0;    // the tallest solid
    double thickness = 0.0; // along Y
    std::vector<KitSolid> solids;
    std::vector<KitOpening> openings;
    std::vector<CollisionPiece> collision; // bottom and top are Z in the piece's frame
    int count = 0;    // placements of this piece

    bool operator==(const KitPiece&) const = default;
};

// Where one copy of a piece goes, in world coordinates (BuildGeometry.h: metres, X = plan x, Y = -plan y, Z up
// from the first floor's level), which is also the frame of every exported FBX before Unreal's import turns it.
struct KitPlacement {
    std::string piece;    // KitPiece::id
    Id floor;
    Id wall;              // the wall for Wall, Opening and Curved pieces; empty for corners
    Id point;             // the plan point of a corner's origin; empty otherwise
    Vec3 location;        // where the piece's origin goes, in the frame rule 9 picked
    double rotationZ = 0.0; // of that frame's X from world X: radians, anticlockwise seen from above

    bool operator==(const KitPlacement&) const = default;
};

struct KitFloorGrid {
    Id floor;
    double elevation = 0.0;
    double gridSize = 0.0;

    bool operator==(const KitFloorGrid&) const = default;
};

struct ModularKit {
    std::vector<KitPiece> pieces;         // sorted by id
    std::vector<KitPlacement> placements; // in the order found (rule 9)
    std::vector<KitFloorGrid> floors;     // every floor with walls, in plan order

    bool operator==(const ModularKit&) const = default;
};

// Works out the kit of a project from its build plan (makeBuildPlan of the same project): the floors' wall
// solids and openings, and their materials and slots. Empty when no floor has walls.
ModularKit modularKit(const Project& project, const BuildPlan& plan);

// The layout file's form, schema/kit-layout.schema.json: {"format", "version", "project", "kit_file",
// "coordinates": "blender_world_metres", "floors": [{"id", "elevation", "grid_size"}],
// "pieces": [{"id", "mesh", "kind", "length", "height", "thickness", "count"}],
// "placements": [{"piece", "mesh", "floor", "wall", "point", "location": [x, y, z], "rotation_z_degrees"}]}.
// wall and point are null when empty; rotation in degrees, anticlockwise seen from above, in [0, 360).
Json kitLayoutToJson(const ModularKit& kit, std::string_view projectName, std::string_view kitFile);

}
