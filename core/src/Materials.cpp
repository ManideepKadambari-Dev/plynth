#include "plynth/core/Materials.h"

#include "plynth/core/BuildGeometry.h"
#include "plynth/core/Geometry.h"
#include "plynth/core/Openings.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace plynth {

namespace {

// A material taken from a wall or a room, or empty when the object names none.
std::optional<ResolvedMaterial> fromObject(const std::optional<std::string>& value, MaterialOrigin origin, const Id& from, Surface surface)
{
    if (!value) return std::nullopt;
    ResolvedMaterial out;
    out.material = value;
    out.origin = origin;
    out.from = from;
    out.surface = surface;
    return out;
}

// Says whether a position is inside rings by the even odd rule.
bool insideRings(Vec2 position, const std::vector<std::vector<Vec2>>& rings)
{
    int count = 0;
    for (const std::vector<Vec2>& ring : rings) {
        if (ring.size() >= 3 && insidePolygon(position, ring)) ++count;
    }
    return count % 2 == 1;
}

// The room of a floor whose floor area holds a position.
const Room* roomHolding(const Floor& floor, Vec2 position)
{
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (shape.closed && insideRings(position, shape.floorArea)) return &room;
    }
    return nullptr;
}

// The floor that holds a wall id, or null.
const Floor* floorOfWall(const Plan& plan, std::string_view wallId)
{
    for (const Floor& floor : plan.floors) {
        if (findWall(floor, wallId)) return &floor;
    }
    return nullptr;
}

// The first room of a floor that lists a wall, or null.
const Room* firstRoomOfWall(const Floor& floor, const Id& wallId)
{
    for (const Room& room : floor.rooms) {
        if (std::find(room.walls.begin(), room.walls.end(), wallId) != room.walls.end()) return &room;
    }
    return nullptr;
}

// The room with an id on any floor, or null.
const Room* findRoomAnywhere(const Plan& plan, std::string_view id)
{
    for (const Floor& floor : plan.floors) {
        if (const Room* room = findRoom(floor, id)) return room;
    }
    return nullptr;
}

// The wall with an id on any floor, or null.
const Wall* findWallAnywhere(const Plan& plan, std::string_view id)
{
    for (const Floor& floor : plan.floors) {
        if (const Wall* wall = findWall(floor, id)) return wall;
    }
    return nullptr;
}

// The stair with an id on any floor, or null.
const Stair* findStairAnywhere(const Plan& plan, std::string_view id)
{
    for (const Floor& floor : plan.floors) {
        if (const Stair* stair = findStair(floor, id)) return stair;
    }
    return nullptr;
}

// Says whether a text is a wall or room style id: the prefix letter and digits.
bool looksLikeId(std::string_view text, std::string_view prefix)
{
    if (text.size() <= prefix.size() || text.substr(0, prefix.size()) != prefix) return false;
    for (char c : text.substr(prefix.size())) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

}

// The file name of an origin.
std::string_view materialOriginName(MaterialOrigin origin)
{
    switch (origin) {
    case MaterialOrigin::Wall: return "wall";
    case MaterialOrigin::Room: return "room";
    case MaterialOrigin::Default: return "default";
    case MaterialOrigin::None: return "none";
    case MaterialOrigin::Stair: return "stair";
    case MaterialOrigin::Roof: return "roof";
    }
    return "none";
}

// The project default for a surface.
ResolvedMaterial surfaceDefaultMaterial(const Project& project, Surface surface)
{
    ResolvedMaterial out;
    out.surface = surface;
    const std::optional<std::string>& value = materialDefault(project.settings.materials, surface);
    if (value) {
        out.material = value;
        out.origin = MaterialOrigin::Default;
    }
    return out;
}

// The material of a wall's faces that look into a room, or into no room.
ResolvedMaterial wallFaceMaterial(const Project& project, const Wall& wall, const Room* room)
{
    if (room) {
        if (auto m = fromObject(wall.materialInside, MaterialOrigin::Wall, wall.id, Surface::WallInside)) return *m;
        if (auto m = fromObject(room->wallMaterial, MaterialOrigin::Room, room->id, Surface::WallInside)) return *m;
        return surfaceDefaultMaterial(project, Surface::WallInside);
    }
    if (auto m = fromObject(wall.materialOutside, MaterialOrigin::Wall, wall.id, Surface::WallOutside)) return *m;
    return surfaceDefaultMaterial(project, Surface::WallOutside);
}

// What the faces of a room's walls show where the wall names none.
ResolvedMaterial roomWallMaterial(const Project& project, const Room& room)
{
    if (auto m = fromObject(room.wallMaterial, MaterialOrigin::Room, room.id, Surface::WallInside)) return *m;
    return surfaceDefaultMaterial(project, Surface::WallInside);
}

// What a room's floor shows.
ResolvedMaterial roomFloorMaterial(const Project& project, const Room& room)
{
    if (auto m = fromObject(room.floorMaterial, MaterialOrigin::Room, room.id, Surface::Floor)) return *m;
    return surfaceDefaultMaterial(project, Surface::Floor);
}

// What a room's ceiling shows.
ResolvedMaterial roomCeilingMaterial(const Project& project, const Room& room)
{
    if (auto m = fromObject(room.ceilingMaterial, MaterialOrigin::Room, room.id, Surface::Ceiling)) return *m;
    return surfaceDefaultMaterial(project, Surface::Ceiling);
}

// The rooms on each side of an opening.
OpeningRooms openingRooms(const Floor& floor, const Opening& opening)
{
    OpeningRooms out;
    const Wall* wall = findWall(floor, opening.wall);
    if (!wall) return out;
    const OpeningPlacement placement = openingPlacement(floor, opening);
    if (!placement.valid) return out;

    const WallCurve curve = wallCurve(floor, *wall);
    const double offset = std::clamp(opening.offset, 0.0, curve.length);
    const Vec2 right = rightOf(tangentAlong(curve, offset));
    const double reach = wall->thickness / 2.0 + kBuildFaceProbe;
    const Vec2 onRight{placement.centre.x + right.x * reach, placement.centre.y + right.y * reach};
    const Vec2 onLeft{placement.centre.x - right.x * reach, placement.centre.y - right.y * reach};
    if (const Room* room = roomHolding(floor, onRight)) out.right = room->id;
    if (const Room* room = roomHolding(floor, onLeft)) out.left = room->id;
    return out;
}

// The materials of one opening.
OpeningMaterials openingMaterials(const Project& project, const Floor& floor, const Opening& opening)
{
    OpeningMaterials out;
    out.door = surfaceDefaultMaterial(project, Surface::Door);
    out.frame = surfaceDefaultMaterial(project, Surface::Frame);
    out.glass = surfaceDefaultMaterial(project, Surface::Glass);
    out.handle = surfaceDefaultMaterial(project, Surface::Handle);
    out.threshold = surfaceDefaultMaterial(project, Surface::Floor);

    const Wall* wall = findWall(floor, opening.wall);
    if (!wall) {
        out.reveal = surfaceDefaultMaterial(project, Surface::WallInside);
        return out;
    }
    const OpeningRooms rooms = openingRooms(floor, opening);
    const Room* left = rooms.left ? findRoom(floor, *rooms.left) : nullptr;
    const Room* right = rooms.right ? findRoom(floor, *rooms.right) : nullptr;

    out.reveal = right ? wallFaceMaterial(project, *wall, right) : wallFaceMaterial(project, *wall, left);

    const Room* swingInto = opening.swing == WallSide::Right ? right : left;
    const Room* other = opening.swing == WallSide::Right ? left : right;
    if (swingInto) {
        out.threshold = roomFloorMaterial(project, *swingInto);
    } else if (other) {
        out.threshold = roomFloorMaterial(project, *other);
    }
    return out;
}

// The material of a stair.
ResolvedMaterial stairMaterial(const Project& project, const Stair& stair)
{
    if (auto m = fromObject(stair.material, MaterialOrigin::Stair, stair.id, Surface::Stair)) return *m;
    return surfaceDefaultMaterial(project, Surface::Stair);
}

// The material of one part of a roof section.
ResolvedMaterial roofPartMaterial(const Project& project, const RoofArea& area, RoofPart part)
{
    switch (part) {
    case RoofPart::Covering: {
        const bool flat = area.settings.type == kFlatRoofId || !findRoofType(area.settings.type);
        const Surface surface = flat ? Surface::FlatRoof : Surface::Roof;
        if (auto m = fromObject(area.material, MaterialOrigin::Roof, area.section, surface)) return *m;
        return surfaceDefaultMaterial(project, surface);
    }
    case RoofPart::Underside: return surfaceDefaultMaterial(project, Surface::Ceiling);
    case RoofPart::Edge: return surfaceDefaultMaterial(project, Surface::WallTop);
    case RoofPart::Gable:
    case RoofPart::Parapet: return surfaceDefaultMaterial(project, Surface::WallOutside);
    case RoofPart::Glazing: return surfaceDefaultMaterial(project, Surface::Glass);
    }
    return {};
}

// The materials of a floor's slab.
SlabMaterials slabMaterials(const Project& project)
{
    SlabMaterials out;
    out.edge = surfaceDefaultMaterial(project, Surface::WallOutside);
    out.well = surfaceDefaultMaterial(project, Surface::WallInside);
    out.underside = surfaceDefaultMaterial(project, Surface::Ceiling);
    return out;
}

// Every material id the build of this project would use.
std::vector<std::string> materialsUsed(const Project& project)
{
    std::set<std::string> used;
    const auto add = [&used](const ResolvedMaterial& m) {
        if (m.material) used.insert(*m.material);
    };

    // The roof, flat roof and stair defaults count only where a section or a stair uses them.
    for (const Surface surface : allSurfaces()) {
        if (surface == Surface::Roof || surface == Surface::FlatRoof || surface == Surface::Stair) continue;
        add(surfaceDefaultMaterial(project, surface));
    }
    const SlabMaterials slab = slabMaterials(project);
    add(slab.edge);
    add(slab.well);
    add(slab.underside);
    for (const RoofArea& area : roofAreas(project.plan)) {
        for (const RoofPart part : {RoofPart::Covering, RoofPart::Underside, RoofPart::Edge, RoofPart::Gable, RoofPart::Parapet,
                                    RoofPart::Glazing}) {
            add(roofPartMaterial(project, area, part));
        }
    }

    for (const Floor& floor : project.plan.floors) {
        for (const Wall& wall : floor.walls) {
            int roomCount = 0;
            for (const Room& room : floor.rooms) {
                if (std::find(room.walls.begin(), room.walls.end(), wall.id) == room.walls.end()) continue;
                ++roomCount;
                add(wallFaceMaterial(project, wall, &room));
            }
            if (roomCount < 2) add(wallFaceMaterial(project, wall, nullptr));
        }
        for (const Room& room : floor.rooms) {
            add(roomFloorMaterial(project, room));
            add(roomCeilingMaterial(project, room));
        }
        for (const Stair& stair : floor.stairs) add(stairMaterial(project, stair));
        for (const Opening& opening : floor.openings) {
            const OpeningMaterials m = openingMaterials(project, floor, opening);
            add(m.reveal);
            add(m.threshold);
            add(m.door);
            add(m.frame);
            add(m.glass);
            add(m.handle);
        }
    }
    return {used.begin(), used.end()};
}

// "inside" or "outside".
std::string_view wallFaceName(WallFace face)
{
    return face == WallFace::Inside ? "inside" : "outside";
}

// The wall face for a name.
std::optional<WallFace> wallFaceFromName(std::string_view name)
{
    if (name == "inside") return WallFace::Inside;
    if (name == "outside") return WallFace::Outside;
    return std::nullopt;
}

// "floor", "wall" or "ceiling".
std::string_view roomSurfaceName(RoomSurface surface)
{
    switch (surface) {
    case RoomSurface::Floor: return "floor";
    case RoomSurface::Walls: return "wall";
    case RoomSurface::Ceiling: return "ceiling";
    }
    return "floor";
}

// The room surface for a name.
std::optional<RoomSurface> roomSurfaceFromName(std::string_view name)
{
    if (name == "floor") return RoomSurface::Floor;
    if (name == "wall") return RoomSurface::Walls;
    if (name == "ceiling") return RoomSurface::Ceiling;
    return std::nullopt;
}

// Reads a target such as "w12.inside", "r3.floor", "hall.wall" or "default.wall_outside".
MaterialTargetParse parseMaterialTarget(const Plan& plan, std::string_view text)
{
    MaterialTargetParse out;
    if (looksLikeId(text, kStairIdPrefix) || looksLikeId(text, kRoofSectionIdPrefix)) {
        const bool stair = looksLikeId(text, kStairIdPrefix);
        if (stair ? findStairAnywhere(plan, text) == nullptr : findRoofSection(plan, text) == nullptr) {
            out.status = fail(ErrorCode::NotFound, std::string(stair ? "There is no stair " : "There is no roof section ") + std::string(text) + ".");
            return out;
        }
        out.target.kind = stair ? MaterialTarget::Kind::Stair : MaterialTarget::Kind::Roof;
        out.target.object = std::string(text);
        return out;
    }
    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= text.size() || text.find('.', dot + 1) != std::string_view::npos) {
        out.status = fail(ErrorCode::InvalidValue,
                          "The target \"" + std::string(text) + "\" is not valid. Write it as w3.inside, r2.floor or default.floor.");
        return out;
    }
    const std::string_view head = text.substr(0, dot);
    const std::string_view tail = text.substr(dot + 1);

    if (head == "default") {
        const std::optional<Surface> surface = surfaceFromName(tail);
        if (!surface) {
            out.status = fail(ErrorCode::InvalidValue, "\"" + std::string(tail) + "\" is not a surface with a default material.");
            return out;
        }
        out.target.kind = MaterialTarget::Kind::Default;
        out.target.surface = *surface;
        return out;
    }

    const auto badPart = [&](const char* kind) {
        out.status = fail(ErrorCode::InvalidValue,
                          "\"" + std::string(tail) + "\" is not a " + kind + " you can set a material on.");
    };

    if (const Wall* wall = findWallAnywhere(plan, head)) {
        const std::optional<WallFace> face = wallFaceFromName(tail);
        if (!face) {
            badPart("face of a wall (use inside or outside)");
            return out;
        }
        out.target.kind = MaterialTarget::Kind::Wall;
        out.target.object = wall->id;
        out.target.wallFace = *face;
        return out;
    }

    const Room* room = findRoomAnywhere(plan, head);
    if (!room && looksLikeId(head, kWallIdPrefix)) {
        out.status = fail(ErrorCode::NotFound, "There is no wall " + std::string(head) + ".");
        return out;
    }
    if (!room && looksLikeId(head, kRoomIdPrefix)) {
        out.status = fail(ErrorCode::NotFound, "There is no room " + std::string(head) + ".");
        return out;
    }
    if (!room) {
        std::vector<const Room*> matches;
        for (const Floor& floor : plan.floors) {
            for (const Room& candidate : floor.rooms) {
                if (candidate.type && *candidate.type == head) matches.push_back(&candidate);
            }
        }
        if (matches.empty()) {
            out.status = fail(ErrorCode::NotFound, "There is no room or wall \"" + std::string(head) + "\".");
            return out;
        }
        if (matches.size() > 1) {
            std::string ids;
            for (const Room* match : matches) ids += (ids.empty() ? "" : ", ") + match->id;
            out.status = fail(ErrorCode::InvalidValue,
                              "More than one room is a " + std::string(head) + " (" + ids + "). Use a room id.");
            return out;
        }
        room = matches.front();
    }

    const std::optional<RoomSurface> surface = roomSurfaceFromName(tail);
    if (!surface) {
        badPart("surface of a room (use floor, wall or ceiling)");
        return out;
    }
    out.target.kind = MaterialTarget::Kind::Room;
    out.target.object = room->id;
    out.target.roomSurface = *surface;
    return out;
}

// What a target shows now.
ResolvedMaterial materialAt(const Project& project, const MaterialTarget& target)
{
    switch (target.kind) {
    case MaterialTarget::Kind::Default: return surfaceDefaultMaterial(project, target.surface);
    case MaterialTarget::Kind::Stair: {
        const Stair* stair = findStairAnywhere(project.plan, target.object);
        return stair ? stairMaterial(project, *stair) : ResolvedMaterial{};
    }
    case MaterialTarget::Kind::Roof: {
        const RoofSection* entry = findRoofSection(project.plan, target.object);
        if (!entry) return {};
        RoofArea area;
        area.floor = entry->floor;
        area.section = entry->id;
        area.settings = entry->settings;
        area.material = entry->material;
        return roofPartMaterial(project, area, RoofPart::Covering);
    }
    case MaterialTarget::Kind::Wall: {
        const Wall* wall = findWallAnywhere(project.plan, target.object);
        if (!wall) return {};
        if (target.wallFace == WallFace::Outside) return wallFaceMaterial(project, *wall, nullptr);
        const Floor* floor = floorOfWall(project.plan, wall->id);
        const Room* room = floor ? firstRoomOfWall(*floor, wall->id) : nullptr;
        return wallFaceMaterial(project, *wall, room);
    }
    case MaterialTarget::Kind::Room: {
        const Room* room = findRoomAnywhere(project.plan, target.object);
        if (!room) return {};
        switch (target.roomSurface) {
        case RoomSurface::Floor: return roomFloorMaterial(project, *room);
        case RoomSurface::Walls: return roomWallMaterial(project, *room);
        case RoomSurface::Ceiling: return roomCeilingMaterial(project, *room);
        }
    }
    }
    return {};
}

}
