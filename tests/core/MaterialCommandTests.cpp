#include "MaterialFixtures.h"
#include "TestSupport.h"

#include <plynth/core/commands/MaterialCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace plynth;
using plynth::test::splitRoomProject;

namespace {

// Applies a command, checks undo puts the project back and redo gives the same result.
void requireRoundTrip(Command& cmd, Project& p)
{
    const Project before = p;
    plynth::test::requireOk(cmd.apply(p));
    const Project after = p;
    REQUIRE_FALSE(after == before);
    plynth::test::requirePlanValid(p.plan);
    cmd.undo(p);
    REQUIRE(p == before);
    plynth::test::requireOk(cmd.apply(p));
    REQUIRE(p == after);
    plynth::test::requirePlanValid(p.plan);
}

// Expects the command to be refused and the project to stay as it was.
void requireRefused(Command& cmd, Project& p, ErrorCode code)
{
    const Project before = p;
    const Status s = cmd.apply(p);
    REQUIRE(s.code == code);
    REQUIRE_FALSE(s.message.empty());
    REQUIRE(p == before);
}

}

TEST_CASE("SetWallMaterialCommand sets, undoes and redoes", "[core][materials][commands]") {
    for (const WallFace face : {WallFace::Inside, WallFace::Outside}) {
        Project p = splitRoomProject();
        SetWallMaterialCommand cmd("w3", face, "plaster");
        requireRoundTrip(cmd, p);
        const Wall& w = *findWall(p.plan.floors[0], "w3");
        REQUIRE((face == WallFace::Inside ? w.materialInside : w.materialOutside) == "plaster");
        REQUIRE_FALSE((face == WallFace::Inside ? w.materialOutside : w.materialInside).has_value());
        REQUIRE(cmd.changes() == Change::Walls);
        REQUIRE(cmd.floorId() == "f1");
        REQUIRE_FALSE(cmd.name().empty());
    }
}

TEST_CASE("SetWallMaterialCommand clears a material and undo brings it back", "[core][materials][commands]") {
    Project p = splitRoomProject();
    findWall(p.plan.floors[0], "w3")->materialOutside = "stucco";
    SetWallMaterialCommand cmd("w3", WallFace::Outside, std::nullopt);
    requireRoundTrip(cmd, p);
    REQUIRE_FALSE(findWall(p.plan.floors[0], "w3")->materialOutside.has_value());
    cmd.undo(p);
    REQUIRE(findWall(p.plan.floors[0], "w3")->materialOutside == "stucco");
}

TEST_CASE("SetWallMaterialCommand refuses a bad name or an unknown wall", "[core][materials][commands]") {
    Project p = splitRoomProject();
    SetWallMaterialCommand bad("w3", WallFace::Inside, "not valid");
    requireRefused(bad, p, ErrorCode::InvalidValue);
    SetWallMaterialCommand empty("w3", WallFace::Inside, std::string());
    requireRefused(empty, p, ErrorCode::InvalidValue);
    SetWallMaterialCommand unknown("w99", WallFace::Inside, "plaster");
    requireRefused(unknown, p, ErrorCode::NotFound);
}

TEST_CASE("SetRoomMaterialCommand sets each surface, undoes and redoes", "[core][materials][commands]") {
    for (const RoomSurface surface : {RoomSurface::Floor, RoomSurface::Walls, RoomSurface::Ceiling}) {
        Project p = splitRoomProject();
        SetRoomMaterialCommand cmd("r2", surface, "tile");
        requireRoundTrip(cmd, p);
        const Room& r = *findRoom(p.plan.floors[0], "r2");
        const std::optional<std::string>& value =
            surface == RoomSurface::Floor ? r.floorMaterial : surface == RoomSurface::Walls ? r.wallMaterial : r.ceilingMaterial;
        REQUIRE(value == "tile");
        const int set = (r.floorMaterial ? 1 : 0) + (r.wallMaterial ? 1 : 0) + (r.ceilingMaterial ? 1 : 0);
        REQUIRE(set == 1);
        REQUIRE(cmd.changes() == Change::Rooms);
        REQUIRE(cmd.floorId() == "f1");
        REQUIRE_FALSE(cmd.name().empty());
    }
}

TEST_CASE("SetRoomMaterialCommand clears and refuses", "[core][materials][commands]") {
    Project p = splitRoomProject();
    findRoom(p.plan.floors[0], "r1")->wallMaterial = "tile";
    SetRoomMaterialCommand clear("r1", RoomSurface::Walls, std::nullopt);
    requireRoundTrip(clear, p);
    REQUIRE_FALSE(findRoom(p.plan.floors[0], "r1")->wallMaterial.has_value());

    SetRoomMaterialCommand bad("r1", RoomSurface::Floor, "no spaces");
    requireRefused(bad, p, ErrorCode::InvalidValue);
    SetRoomMaterialCommand unknown("r99", RoomSurface::Floor, "tile");
    requireRefused(unknown, p, ErrorCode::NotFound);
}

TEST_CASE("SetDefaultMaterialCommand sets each surface, undoes and redoes", "[core][materials][commands]") {
    for (const Surface surface : allSurfaces()) {
        Project p = makeNewProject();
        SetDefaultMaterialCommand cmd(surface, "grey_tile");
        requireRoundTrip(cmd, p);
        REQUIRE(materialDefault(p.settings.materials, surface) == "grey_tile");
        REQUIRE(cmd.changes() == Change::Settings);
        REQUIRE(cmd.floorId().empty());
        REQUIRE_FALSE(cmd.name().empty());
    }
}

TEST_CASE("SetDefaultMaterialCommand clears and refuses", "[core][materials][commands]") {
    Project p = makeNewProject();
    SetDefaultMaterialCommand clear(Surface::Ceiling, std::nullopt);
    requireRoundTrip(clear, p);
    REQUIRE_FALSE(materialDefault(p.settings.materials, Surface::Ceiling).has_value());
    clear.undo(p);
    REQUIRE(materialDefault(p.settings.materials, Surface::Ceiling) == "white_paint");

    SetDefaultMaterialCommand bad(Surface::Floor, "a b");
    requireRefused(bad, p, ErrorCode::InvalidValue);
}
