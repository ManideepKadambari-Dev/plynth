#pragma once

#include "BuildGeometry.h"
#include "MaterialDefaults.h"
#include "Plan.h"
#include "Project.h"
#include "Roofs.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Which material each built surface gets (DESIGN.md sections 7.2 and 7.13 step 8). The most specific mark
// wins:
//   a wall face looking into room r: wall.materialInside, then r.wallMaterial, then the wall_inside default
//   a wall face looking into no room: wall.materialOutside, then the wall_outside default
//   wall tops and bottoms: the wall_top default
//   a room's floor: room.floorMaterial, then the floor default; its ceiling: room.ceilingMaterial, then the
//   ceiling default
//   the faces a cut makes in a wall (reveals): what the wall's face on the opening's right side shows, or on
//   its left side when no room is on the right (see openingRooms)
//   a doorway's threshold: the floor of the room on the door's swing side, then of the room on the other
//   side, then the floor default
//   door leaves, frames, glass and handles: their defaults
//   a stair (every step, landing and column): stair.material, then the stair default
//   a roof section's covering: the section entry's material, then the roof default for pitched types or the
//   flat_roof default for a flat roof; its underside: the ceiling default; its edges: the wall_top default;
//   its gable fills and parapet: the wall_outside default; sawtooth glazing: the glass default
//   a slab's outer side faces: the wall_outside default; its stair well sides: the wall_inside default; its
//   undersides (below an upper floor that reaches past the one below): the ceiling default
// The ids are catalog material ids. Nothing here opens the library; the build reports ids it lacks.

// Where a material came from.
enum class MaterialOrigin {
    Wall,    // "wall": the wall names it
    Room,    // "room": the room names it
    Default, // "default": the project default for the surface
    None,    // "none": nothing names one; the build uses the neutral look
    Stair,   // "stair": the stair names it
    Roof,    // "roof": the roof section entry names it
};

std::string_view materialOriginName(MaterialOrigin origin);

struct ResolvedMaterial {
    std::optional<std::string> material;
    MaterialOrigin origin = MaterialOrigin::None;
    Id from;                 // the wall or room that names it; empty for Default and None
    Surface surface = Surface::WallInside; // the surface whose default applies when origin is Default

    bool operator==(const ResolvedMaterial&) const = default;
};

// The material of a wall's faces that look into room, or into no room when room is null.
ResolvedMaterial wallFaceMaterial(const Project& project, const Wall& wall, const Room* room);

// What the faces of a room's walls show where the wall names no inside material.
ResolvedMaterial roomWallMaterial(const Project& project, const Room& room);
ResolvedMaterial roomFloorMaterial(const Project& project, const Room& room);
ResolvedMaterial roomCeilingMaterial(const Project& project, const Room& room);

// The project default for a surface, with origin Default, or None when the default is empty.
ResolvedMaterial surfaceDefaultMaterial(const Project& project, Surface surface);

// The rooms on each side of an opening: the room whose floor area holds the point kBuildFaceProbe beyond
// each face of the wall at the opening's centre. Empty when no room is there.
struct OpeningRooms {
    std::optional<Id> left;
    std::optional<Id> right;

    bool operator==(const OpeningRooms&) const = default;
};

OpeningRooms openingRooms(const Floor& floor, const Opening& opening);

struct OpeningMaterials {
    ResolvedMaterial reveal;
    ResolvedMaterial threshold; // used only when the sill is 0
    ResolvedMaterial door;
    ResolvedMaterial frame;
    ResolvedMaterial glass;
    ResolvedMaterial handle;

    bool operator==(const OpeningMaterials&) const = default;
};

// The materials of one opening by the rules above. The opening's wall must be on the floor.
OpeningMaterials openingMaterials(const Project& project, const Floor& floor, const Opening& opening);

ResolvedMaterial stairMaterial(const Project& project, const Stair& stair);

// The material of one part of a roof section by the rules above (from is the section entry's id for Roof).
ResolvedMaterial roofPartMaterial(const Project& project, const RoofArea& area, RoofPart part);

// The materials of a floor's slab by the rules above.
struct SlabMaterials {
    ResolvedMaterial edge;      // outer side faces
    ResolvedMaterial well;      // side faces of stair holes
    ResolvedMaterial underside; // faces below an upper floor that reaches past the floor below

    bool operator==(const SlabMaterials&) const = default;
};

SlabMaterials slabMaterials(const Project& project);

// Every material id the build of this project would use, sorted, without repeats: every wall face that looks
// into a room or outside, every room's floor and ceiling, wall tops, the opening materials, every stair, every
// roof section's parts and the slab materials, plus each set project default. For "plynth material list" and
// the build's library check.
std::vector<std::string> materialsUsed(const Project& project);

struct GeneratorLibrary; // Generators.h

// The same, plus the materials of generator items, found with the library's declarations.
std::vector<std::string> materialsUsed(const Project& project, const GeneratorLibrary& generators);

// The two faces of a wall a material can be set on.
enum class WallFace { Inside, Outside };

// "inside" or "outside", and back. Empty for an unknown name.
std::string_view wallFaceName(WallFace face);
std::optional<WallFace> wallFaceFromName(std::string_view name);

// The three surfaces of a room a material can be set on.
enum class RoomSurface { Floor, Walls, Ceiling };

// "floor", "wall" or "ceiling", and back. Empty for an unknown name.
std::string_view roomSurfaceName(RoomSurface surface);
std::optional<RoomSurface> roomSurfaceFromName(std::string_view name);

// A place a material can be set, as the CLI writes it: "w12.inside", "w12.outside", "r3.floor", "r3.wall",
// "r3.ceiling", "default.<surface>" such as "default.wall_outside", a stair id such as "s2" (its steps), or a
// roof section entry id such as "rf1" (its covering). For a room, the part before the dot may also be a room
// type when exactly one room of the plan has it ("hall.floor").
struct MaterialTarget {
    enum class Kind { Wall, Room, Default, Stair, Roof } kind = Kind::Wall;
    Id object;                          // the wall, room, stair or roof section entry id; empty for Default
    WallFace wallFace = WallFace::Inside;
    RoomSurface roomSurface = RoomSurface::Floor;
    Surface surface = Surface::WallInside;

    bool operator==(const MaterialTarget&) const = default;
};

struct MaterialTargetParse {
    Status status; // InvalidValue for a malformed text or a room type that matches no room or several (naming
                   // their ids), NotFound for an unknown wall or room id
    MaterialTarget target;
};

MaterialTargetParse parseMaterialTarget(const Plan& plan, std::string_view text);

// What a target shows now, by the rules above (for a wall's inside: as seen from its first room, or with no
// room when it has none).
ResolvedMaterial materialAt(const Project& project, const MaterialTarget& target);

}
