#include "MaterialFixtures.h"
#include "TestSupport.h"

#include <plynth/core/MaterialDefaults.h>
#include <plynth/core/Materials.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace plynth;
using plynth::test::splitRoomProject;

namespace {

Room& room(Project& p, const std::string& id)
{
    return *findRoom(p.plan.floors[0], id);
}

Wall& wall(Project& p, const std::string& id)
{
    return *findWall(p.plan.floors[0], id);
}

}

TEST_CASE("the surfaces have names that read back", "[core][materials]") {
    REQUIRE(allSurfaces().size() == 9);
    for (const Surface s : allSurfaces()) {
        REQUIRE(surfaceFromName(surfaceName(s)) == s);
    }
    REQUIRE(surfaceName(Surface::WallInside) == "wall_inside");
    REQUIRE(surfaceName(Surface::Handle) == "handle");
    REQUIRE_FALSE(surfaceFromName("roof").has_value());
}

TEST_CASE("material defaults start as the sample materials and can change", "[core][materials]") {
    MaterialDefaults d;
    REQUIRE(materialDefault(d, Surface::WallInside) == "white_paint");
    REQUIRE(materialDefault(d, Surface::WallOutside) == "red_brick");
    REQUIRE(materialDefault(d, Surface::WallTop) == "white_paint");
    REQUIRE(materialDefault(d, Surface::Floor) == "oak_planks");
    REQUIRE(materialDefault(d, Surface::Ceiling) == "white_paint");
    REQUIRE(materialDefault(d, Surface::Door) == "painted_wood");
    REQUIRE(materialDefault(d, Surface::Frame) == "white_paint");
    REQUIRE(materialDefault(d, Surface::Glass) == "clear_glass");
    REQUIRE(materialDefault(d, Surface::Handle) == "brushed_steel");

    for (const Surface s : allSurfaces()) {
        MaterialDefaults changed;
        setMaterialDefault(changed, s, "tile");
        REQUIRE(materialDefault(changed, s) == "tile");
        setMaterialDefault(changed, s, std::nullopt);
        REQUIRE_FALSE(materialDefault(changed, s).has_value());
    }
}

TEST_CASE("checkMaterialDefaults names the surface with a bad name", "[core][materials]") {
    MaterialDefaults d;
    plynth::test::requireOk(checkMaterialDefaults(d));
    setMaterialDefault(d, Surface::Glass, "not valid");
    const Status s = checkMaterialDefaults(d);
    REQUIRE(s.code == ErrorCode::InvalidValue);
    REQUIRE(s.where == "/materials/glass");
    REQUIRE(s.message.find("glass") != std::string::npos);
    setMaterialDefault(d, Surface::Glass, std::string(129, 'a'));
    REQUIRE_FALSE(checkMaterialDefaults(d).ok());
    setMaterialDefault(d, Surface::Glass, "");
    REQUIRE_FALSE(checkMaterialDefaults(d).ok());
    setMaterialDefault(d, Surface::Glass, std::nullopt);
    plynth::test::requireOk(checkMaterialDefaults(d));
}

TEST_CASE("the fixture has two rooms and a door between them", "[core][materials]") {
    Project p = splitRoomProject();
    REQUIRE(p.plan.floors[0].rooms.size() == 2);
    REQUIRE(room(p, "r1").walls.size() == 4);
    REQUIRE(room(p, "r2").walls.size() == 4);
    plynth::test::requireOk(checkPlan(p.plan));
}

TEST_CASE("a wall face looking into a room takes the wall, the room, then the default", "[core][materials]") {
    Project p = splitRoomProject();

    SECTION("nothing set gives the project default") {
        const ResolvedMaterial m = wallFaceMaterial(p, wall(p, "w7"), &room(p, "r1"));
        REQUIRE(m.material == "white_paint");
        REQUIRE(m.origin == MaterialOrigin::Default);
        REQUIRE(m.from.empty());
        REQUIRE(m.surface == Surface::WallInside);
    }
    SECTION("the room's wall material applies to faces into that room only") {
        room(p, "r1").wallMaterial = "tile";
        const ResolvedMaterial into1 = wallFaceMaterial(p, wall(p, "w7"), &room(p, "r1"));
        REQUIRE(into1.material == "tile");
        REQUIRE(into1.origin == MaterialOrigin::Room);
        REQUIRE(into1.from == "r1");
        const ResolvedMaterial into2 = wallFaceMaterial(p, wall(p, "w7"), &room(p, "r2"));
        REQUIRE(into2.material == "white_paint");
        REQUIRE(into2.origin == MaterialOrigin::Default);
        REQUIRE(roomWallMaterial(p, room(p, "r1")).material == "tile");
        REQUIRE(roomWallMaterial(p, room(p, "r2")).origin == MaterialOrigin::Default);
    }
    SECTION("the wall's inside material wins over both rooms") {
        room(p, "r1").wallMaterial = "tile";
        room(p, "r2").wallMaterial = "wallpaper";
        wall(p, "w7").materialInside = "plaster";
        for (const char* id : {"r1", "r2"}) {
            const ResolvedMaterial m = wallFaceMaterial(p, wall(p, "w7"), &room(p, id));
            REQUIRE(m.material == "plaster");
            REQUIRE(m.origin == MaterialOrigin::Wall);
            REQUIRE(m.from == "w7");
        }
    }
    SECTION("a default set to nothing gives none") {
        setMaterialDefault(p.settings.materials, Surface::WallInside, std::nullopt);
        const ResolvedMaterial m = wallFaceMaterial(p, wall(p, "w7"), &room(p, "r1"));
        REQUIRE_FALSE(m.material.has_value());
        REQUIRE(m.origin == MaterialOrigin::None);
        REQUIRE(m.from.empty());
    }
}

TEST_CASE("a face looking into no room takes the outside material, then the default", "[core][materials]") {
    Project p = splitRoomProject();
    ResolvedMaterial m = wallFaceMaterial(p, wall(p, "w1"), nullptr);
    REQUIRE(m.material == "red_brick");
    REQUIRE(m.origin == MaterialOrigin::Default);
    REQUIRE(m.surface == Surface::WallOutside);

    wall(p, "w1").materialOutside = "stucco";
    m = wallFaceMaterial(p, wall(p, "w1"), nullptr);
    REQUIRE(m.material == "stucco");
    REQUIRE(m.origin == MaterialOrigin::Wall);
    REQUIRE(m.from == "w1");

    wall(p, "w1").materialOutside.reset();
    setMaterialDefault(p.settings.materials, Surface::WallOutside, std::nullopt);
    m = wallFaceMaterial(p, wall(p, "w1"), nullptr);
    REQUIRE(m.origin == MaterialOrigin::None);
}

TEST_CASE("floors, ceilings and tops use the room, then the default", "[core][materials]") {
    Project p = splitRoomProject();
    REQUIRE(roomFloorMaterial(p, room(p, "r1")).material == "oak_planks");
    REQUIRE(roomFloorMaterial(p, room(p, "r1")).origin == MaterialOrigin::Default);
    REQUIRE(roomCeilingMaterial(p, room(p, "r1")).material == "white_paint");
    room(p, "r1").floorMaterial = "marble";
    room(p, "r1").ceilingMaterial = "plaster";
    const ResolvedMaterial floor = roomFloorMaterial(p, room(p, "r1"));
    REQUIRE(floor.material == "marble");
    REQUIRE(floor.origin == MaterialOrigin::Room);
    REQUIRE(floor.from == "r1");
    REQUIRE(roomCeilingMaterial(p, room(p, "r1")).material == "plaster");
    REQUIRE(roomFloorMaterial(p, room(p, "r2")).material == "oak_planks");

    const ResolvedMaterial top = surfaceDefaultMaterial(p, Surface::WallTop);
    REQUIRE(top.material == "white_paint");
    REQUIRE(top.origin == MaterialOrigin::Default);
    REQUIRE(top.surface == Surface::WallTop);
    setMaterialDefault(p.settings.materials, Surface::WallTop, std::nullopt);
    REQUIRE(surfaceDefaultMaterial(p, Surface::WallTop).origin == MaterialOrigin::None);
}

TEST_CASE("material origins have names", "[core][materials]") {
    REQUIRE(materialOriginName(MaterialOrigin::Wall) == "wall");
    REQUIRE(materialOriginName(MaterialOrigin::Room) == "room");
    REQUIRE(materialOriginName(MaterialOrigin::Default) == "default");
    REQUIRE(materialOriginName(MaterialOrigin::None) == "none");
}

TEST_CASE("openingRooms finds the room on each side of a door", "[core][materials]") {
    Project p = splitRoomProject();
    Floor& f = p.plan.floors[0];
    const OpeningRooms inner = openingRooms(f, f.openings[0]);
    REQUIRE(inner.right == "r1");
    REQUIRE(inner.left == "r2");

    SECTION("a door in an outer wall has a room on one side only") {
        Opening o = plynth::test::doorOn("w1", 1.0);
        o.id = "o2";
        f.openings.push_back(o);
        const OpeningRooms outer = openingRooms(f, f.openings.back());
        REQUIRE(outer.right == "r1");
        REQUIRE_FALSE(outer.left.has_value());
    }
    SECTION("reversing the wall swaps the sides") {
        Wall& w = *findWall(f, "w7");
        std::swap(w.from, w.to);
        const OpeningRooms swapped = openingRooms(f, f.openings[0]);
        REQUIRE(swapped.right == "r2");
        REQUIRE(swapped.left == "r1");
    }
    SECTION("an opening whose wall is missing has no rooms") {
        Opening o = f.openings[0];
        o.wall = "w99";
        const OpeningRooms none = openingRooms(f, o);
        REQUIRE_FALSE(none.right.has_value());
        REQUIRE_FALSE(none.left.has_value());
    }
}

TEST_CASE("a reveal follows the room on the opening's right side", "[core][materials]") {
    Project p = splitRoomProject();
    room(p, "r1").wallMaterial = "tile";
    room(p, "r2").wallMaterial = "wallpaper";
    Floor& f = p.plan.floors[0];

    OpeningMaterials m = openingMaterials(p, f, f.openings[0]);
    REQUIRE(m.reveal.material == "tile");
    REQUIRE(m.reveal.from == "r1");

    SECTION("with no room on the right the left side is used") {
        Wall& w1 = *findWall(f, "w1");
        std::swap(w1.from, w1.to);
        Opening o = plynth::test::doorOn("w1", 1.0);
        o.id = "o2";
        f.openings.push_back(o);
        const OpeningRooms rooms = openingRooms(f, f.openings.back());
        REQUIRE_FALSE(rooms.right.has_value());
        REQUIRE(rooms.left == "r1");
        m = openingMaterials(p, f, f.openings.back());
        REQUIRE(m.reveal.material == "tile");
    }
    SECTION("an outer door with no room on either side shows the outside material") {
        f.rooms.clear();
        Opening o = plynth::test::doorOn("w1", 1.0);
        o.id = "o2";
        f.openings.push_back(o);
        m = openingMaterials(p, f, f.openings.back());
        REQUIRE(m.reveal.material == "red_brick");
        REQUIRE(m.reveal.origin == MaterialOrigin::Default);
    }
}

TEST_CASE("a threshold follows the swing side, then the other side, then the default", "[core][materials]") {
    Project p = splitRoomProject();
    room(p, "r1").floorMaterial = "marble";
    room(p, "r2").floorMaterial = "carpet";
    Floor& f = p.plan.floors[0];

    SECTION("swing right goes into r1") {
        REQUIRE(openingMaterials(p, f, f.openings[0]).threshold.material == "marble");
    }
    SECTION("swing left goes into r2") {
        f.openings[0].swing = WallSide::Left;
        REQUIRE(openingMaterials(p, f, f.openings[0]).threshold.material == "carpet");
    }
    SECTION("a swing toward outside uses the other side") {
        Opening o = plynth::test::doorOn("w1", 1.0);
        o.id = "o2";
        o.swing = WallSide::Left;
        f.openings.push_back(o);
        const OpeningMaterials m = openingMaterials(p, f, f.openings.back());
        REQUIRE(m.threshold.material == "marble");
        REQUIRE(m.threshold.from == "r1");
    }
    SECTION("no room at all gives the floor default") {
        f.rooms.clear();
        const OpeningMaterials m = openingMaterials(p, f, f.openings[0]);
        REQUIRE(m.threshold.material == "oak_planks");
        REQUIRE(m.threshold.origin == MaterialOrigin::Default);
    }
}

TEST_CASE("door, frame, glass and handle come from their defaults", "[core][materials]") {
    Project p = splitRoomProject();
    const Floor& f = p.plan.floors[0];
    OpeningMaterials m = openingMaterials(p, f, f.openings[0]);
    REQUIRE(m.door.material == "painted_wood");
    REQUIRE(m.frame.material == "white_paint");
    REQUIRE(m.glass.material == "clear_glass");
    REQUIRE(m.handle.material == "brushed_steel");
    REQUIRE(m.door.surface == Surface::Door);
    REQUIRE(m.handle.surface == Surface::Handle);

    setMaterialDefault(p.settings.materials, Surface::Handle, std::nullopt);
    setMaterialDefault(p.settings.materials, Surface::Glass, "frosted");
    m = openingMaterials(p, f, f.openings[0]);
    REQUIRE(m.handle.origin == MaterialOrigin::None);
    REQUIRE(m.glass.material == "frosted");
}

TEST_CASE("materialsUsed is sorted and has no repeats", "[core][materials]") {
    Project p = splitRoomProject();
    const std::vector<std::string> defaults = {"brushed_steel", "clear_glass", "oak_planks", "painted_wood", "red_brick", "white_paint"};
    REQUIRE(materialsUsed(p) == defaults);

    room(p, "r1").wallMaterial = "tile";
    wall(p, "w7").materialInside = "plaster";
    wall(p, "w1").materialOutside = "stucco";
    room(p, "r2").floorMaterial = "carpet";
    room(p, "r2").ceilingMaterial = "tile";
    const std::vector<std::string> expected = {"brushed_steel", "carpet",    "clear_glass", "oak_planks", "painted_wood",
                                               "plaster",       "red_brick", "stucco",      "tile",       "white_paint"};
    REQUIRE(materialsUsed(p) == expected);

    setMaterialDefault(p.settings.materials, Surface::Glass, std::nullopt);
    setMaterialDefault(p.settings.materials, Surface::Door, std::nullopt);
    const std::vector<std::string> fewer = materialsUsed(p);
    REQUIRE(std::find(fewer.begin(), fewer.end(), "clear_glass") == fewer.end());
    REQUIRE(std::find(fewer.begin(), fewer.end(), "painted_wood") == fewer.end());
}

TEST_CASE("wall face and room surface names read back", "[core][materials]") {
    REQUIRE(wallFaceName(WallFace::Inside) == "inside");
    REQUIRE(wallFaceFromName("outside") == WallFace::Outside);
    REQUIRE_FALSE(wallFaceFromName("top").has_value());
    REQUIRE(roomSurfaceName(RoomSurface::Walls) == "wall");
    REQUIRE(roomSurfaceFromName("ceiling") == RoomSurface::Ceiling);
    REQUIRE(roomSurfaceFromName("floor") == RoomSurface::Floor);
    REQUIRE_FALSE(roomSurfaceFromName("walls").has_value());
}

TEST_CASE("parseMaterialTarget reads every form", "[core][materials]") {
    Project p = splitRoomProject();
    room(p, "r1").type = "hall";
    room(p, "r2").type = "kitchen";
    const Plan& plan = p.plan;

    MaterialTargetParse r = parseMaterialTarget(plan, "w3.inside");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.kind == MaterialTarget::Kind::Wall);
    REQUIRE(r.target.object == "w3");
    REQUIRE(r.target.wallFace == WallFace::Inside);

    r = parseMaterialTarget(plan, "w3.outside");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.wallFace == WallFace::Outside);

    r = parseMaterialTarget(plan, "r1.floor");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.kind == MaterialTarget::Kind::Room);
    REQUIRE(r.target.object == "r1");
    REQUIRE(r.target.roomSurface == RoomSurface::Floor);

    r = parseMaterialTarget(plan, "r2.wall");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.roomSurface == RoomSurface::Walls);

    r = parseMaterialTarget(plan, "r2.ceiling");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.roomSurface == RoomSurface::Ceiling);

    r = parseMaterialTarget(plan, "default.wall_outside");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.kind == MaterialTarget::Kind::Default);
    REQUIRE(r.target.surface == Surface::WallOutside);
    REQUIRE(r.target.object.empty());

    r = parseMaterialTarget(plan, "hall.floor");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.kind == MaterialTarget::Kind::Room);
    REQUIRE(r.target.object == "r1");

    r = parseMaterialTarget(plan, "kitchen.wall");
    plynth::test::requireOk(r.status);
    REQUIRE(r.target.object == "r2");
    REQUIRE(r.target.roomSurface == RoomSurface::Walls);
}

TEST_CASE("parseMaterialTarget refuses what it cannot place", "[core][materials]") {
    Project p = splitRoomProject();
    room(p, "r1").type = "bedroom";
    room(p, "r2").type = "bedroom";
    const Plan& plan = p.plan;

    SECTION("a room type with two rooms names both") {
        const MaterialTargetParse r = parseMaterialTarget(plan, "bedroom.floor");
        REQUIRE(r.status.code == ErrorCode::InvalidValue);
        REQUIRE(r.status.message.find("r1") != std::string::npos);
        REQUIRE(r.status.message.find("r2") != std::string::npos);
    }
    SECTION("an unknown wall or room is not found") {
        REQUIRE(parseMaterialTarget(plan, "w99.inside").status.code == ErrorCode::NotFound);
        REQUIRE(parseMaterialTarget(plan, "r99.floor").status.code == ErrorCode::NotFound);
        REQUIRE(parseMaterialTarget(plan, "attic.floor").status.code == ErrorCode::NotFound);
    }
    SECTION("a bad surface or a bad shape is invalid") {
        REQUIRE(parseMaterialTarget(plan, "default.nonsense").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, "w3.floor").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, "r1.inside").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, "r1.walls").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, "w3").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, "").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, ".floor").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, "r1.").status.code == ErrorCode::InvalidValue);
        REQUIRE(parseMaterialTarget(plan, "r1.floor.x").status.code == ErrorCode::InvalidValue);
    }
}

TEST_CASE("materialAt says what a target shows now", "[core][materials]") {
    Project p = splitRoomProject();
    room(p, "r1").wallMaterial = "tile";
    room(p, "r2").floorMaterial = "carpet";
    wall(p, "w1").materialOutside = "stucco";

    const auto at = [&](const char* text) {
        const MaterialTargetParse r = parseMaterialTarget(p.plan, text);
        plynth::test::requireOk(r.status);
        return materialAt(p, r.target);
    };

    REQUIRE(at("r1.wall").material == "tile");
    REQUIRE(at("r1.floor").material == "oak_planks");
    REQUIRE(at("r2.floor").material == "carpet");
    REQUIRE(at("r2.ceiling").material == "white_paint");
    REQUIRE(at("default.floor").material == "oak_planks");
    REQUIRE(at("w1.outside").material == "stucco");
    REQUIRE(at("w1.outside").origin == MaterialOrigin::Wall);
    REQUIRE(at("w3.outside").material == "red_brick");

    SECTION("a wall's inside is seen from its first room") {
        const ResolvedMaterial m = at("w1.inside");
        REQUIRE(m.material == "tile");
        REQUIRE(m.from == "r1");
        REQUIRE(at("w3.inside").material == "white_paint");
        wall(p, "w3").materialInside = "plaster";
        REQUIRE(at("w3.inside").origin == MaterialOrigin::Wall);
    }
    SECTION("a wall in no room is seen with no room, so the outside rules apply") {
        p.plan.floors[0].rooms.clear();
        const ResolvedMaterial m = at("w3.inside");
        REQUIRE(m.material == "red_brick");
        REQUIRE(m.origin == MaterialOrigin::Default);
        wall(p, "w3").materialOutside = "stucco";
        REQUIRE(at("w3.inside").material == "stucco");
    }
    SECTION("an unknown target gives none") {
        MaterialTarget gone;
        gone.kind = MaterialTarget::Kind::Wall;
        gone.object = "w99";
        REQUIRE(materialAt(p, gone).origin == MaterialOrigin::None);
    }
}
