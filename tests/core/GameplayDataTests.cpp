#include "BuildFixtures.h"
#include "RoomFixtures.h"
#include "SchemaSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Items.h>
#include <plynth/core/GameplayData.h>
#include <plynth/core/Rooms.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

// An opening on a wall.
Opening openingOn(const std::string& id, const std::string& wall, OpeningType type, double offset, double width,
                  double height, double sill)
{
    Opening o;
    o.id = id;
    o.wall = wall;
    o.type = type;
    o.offset = offset;
    o.width = width;
    o.height = height;
    o.sill = sill;
    return o;
}

// The split house with rooms r1 (left) and r2 (right), an inner door, an outer door, a window, a plain
// opening on the floor and one with a sill, and a second floor with one room r9.
Project gameplayProject()
{
    Project p = test::splitHouseProject();
    Floor& f = p.plan.floors[0];
    Room r1;
    r1.id = "r1";
    r1.type = "living";
    r1.walls = {"w1", "w7", "w5", "w6"};
    Room r2;
    r2.id = "r2";
    r2.walls = {"w2", "w3", "w4", "w7"};
    f.rooms = {r1, r2};
    f.openings = {openingOn("o1", "w7", OpeningType::Door, 2.0, 0.9, 2.1, 0.0),
                  openingOn("o2", "w1", OpeningType::Door, 1.5, 0.9, 2.1, 0.0),
                  openingOn("o3", "w3", OpeningType::Window, 2.0, 1.2, 1.2, 0.9),
                  openingOn("o4", "w4", OpeningType::Plain, 1.5, 0.9, 2.1, 0.0),
                  openingOn("o5", "w2", OpeningType::Plain, 1.5, 0.9, 1.0, 0.5)};

    Floor f2;
    f2.id = "f2";
    f2.name = "Upstairs";
    test::addRectangle(f2, 9, 0.0, 0.0, 3.0, 4.0, 0.2);
    Room r9;
    r9.id = "r9";
    r9.walls = {"w91", "w92", "w93", "w94"};
    f2.rooms = {r9};
    p.plan.floors.push_back(f2);
    return p;
}

// The gameplay data of a project, built without a library.
GameplayData dataOf(const Project& p, const std::vector<GameplayLight>& lights = {})
{
    const BuildPlan plan = makeBuildPlan(p, test::sampleAssetSource());
    return gameplayData("Flat", p.plan, plan.floors, lights);
}

// Finds a room of the data by id.
const GameplayRoom& roomOf(const GameplayData& data, const std::string& id)
{
    for (const GameplayRoom& r : data.rooms) {
        if (r.id == id) return r;
    }
    FAIL("no room " << id);
    return data.rooms.front();
}

// The total area of convex outlines.
double volumeArea(const std::vector<GameplayVolume>& volumes)
{
    double sum = 0.0;
    for (const GameplayVolume& v : volumes) sum += std::fabs(signedArea(v.outline));
    return sum;
}

// Says whether a world point keeps the spawn clearance from an item's footprint.
bool clearOfItem(Vec3 spawn, const Item& item)
{
    const ItemFootprint fp = itemFootprint(item);
    const Vec2 p{spawn.x, -spawn.y};
    if (insidePolygon(p, fp.corners)) return false;
    for (std::size_t i = 0; i < 4; ++i) {
        if (projectOntoSegment(p, fp.corners[i], fp.corners[(i + 1) % 4]).distance < kSpawnClearance - 1e-9) return false;
    }
    return true;
}

}

TEST_CASE("Gameplay rooms carry their area, bounds and levels", "[gameplay]")
{
    const GameplayData data = dataOf(gameplayProject());
    REQUIRE(data.floors.size() == 2);
    CHECK(data.floors[0].id == "f1");
    CHECK(data.floors[0].elevation == 0.0);
    CHECK_THAT(data.floors[1].elevation, WithinAbs(3.2, 1e-9));
    CHECK(data.floors[1].name == "Upstairs");
    REQUIRE(data.rooms.size() == 3);

    const GameplayRoom& r1 = roomOf(data, "r1");
    CHECK(r1.floor == "f1");
    CHECK(r1.type == std::optional<std::string>("living"));
    CHECK_THAT(r1.area, WithinAbs(2.7 * 3.7, 1e-6));
    CHECK_THAT(r1.boundsMin.x, WithinAbs(0.15, 1e-6));
    CHECK_THAT(r1.boundsMax.x, WithinAbs(2.85, 1e-6));
    CHECK_THAT(r1.boundsMin.y, WithinAbs(-3.85, 1e-6));
    CHECK_THAT(r1.boundsMax.y, WithinAbs(-0.15, 1e-6));
    CHECK(r1.boundsMin.z == 0.0);
    CHECK_THAT(r1.boundsMax.z, WithinAbs(3.0, 1e-9));
    CHECK_FALSE(roomOf(data, "r2").type.has_value());

    const GameplayRoom& r9 = roomOf(data, "r9");
    CHECK(r9.floor == "f2");
    CHECK_THAT(r9.floorZ, WithinAbs(3.2, 1e-9));
    CHECK_THAT(r9.ceilingZ, WithinAbs(6.2, 1e-9));
    CHECK_THAT(r9.boundsMin.z, WithinAbs(3.2, 1e-9));
}

TEST_CASE("Gameplay volumes are convex and cover each room", "[gameplay]")
{
    const GameplayData data = dataOf(gameplayProject());
    for (const GameplayRoom& r : data.rooms) {
        INFO(r.id);
        REQUIRE_FALSE(r.volumes.empty());
        CHECK_THAT(volumeArea(r.volumes), WithinAbs(r.area, 1e-6));
        for (const GameplayVolume& v : r.volumes) {
            CHECK(isConvexRing(v.outline));
            CHECK(v.bottom == r.floorZ);
            CHECK(v.top == r.ceilingZ);
        }
    }
}

TEST_CASE("Gameplay doors know the rooms on each side", "[gameplay]")
{
    const GameplayData data = dataOf(gameplayProject());
    REQUIRE(data.doors.size() == 3);

    const GameplayDoor& inner = data.doors[0];
    CHECK(inner.id == "o1");
    CHECK(inner.kind == "door");
    CHECK(inner.roomFront == std::optional<Id>("r1"));
    CHECK(inner.roomBack == std::optional<Id>("r2"));
    CHECK_THAT(inner.position.x, WithinAbs(3.0, 1e-9));
    CHECK_THAT(inner.position.y, WithinAbs(-2.0, 1e-9));
    CHECK(inner.position.z == 0.0);
    CHECK_THAT(inner.facing.x, WithinAbs(-1.0, 1e-9));
    CHECK_THAT(inner.along.y, WithinAbs(-1.0, 1e-9));
    CHECK(inner.width == 0.9);
    CHECK(inner.height == 2.1);

    const GameplayDoor& outer = data.doors[1];
    CHECK(outer.id == "o2");
    CHECK(outer.roomFront == std::optional<Id>("r1"));
    CHECK_FALSE(outer.roomBack.has_value());

    // The window o3 and the plain opening with a sill o5 are left out; o4 is a passage.
    CHECK(data.doors[2].id == "o4");
    CHECK(data.doors[2].kind == "passage");
    CHECK(data.doors[2].roomFront.has_value() != data.doors[2].roomBack.has_value());
}

TEST_CASE("Gameplay lights are written as given", "[gameplay]")
{
    GameplayLight light;
    light.id = "l1";
    light.floor = "f1";
    light.room = "r1";
    light.kind = LightKind::Ceiling;
    light.position = {1.5, -2.0, 2.9};
    GameplayLight lamp;
    lamp.id = "l2";
    lamp.floor = "f2";
    lamp.kind = LightKind::Standing;
    lamp.position = {1.0, -1.0, 4.6};
    const GameplayData data = dataOf(gameplayProject(), {light, lamp});
    REQUIRE(data.lights.size() == 2);
    CHECK(data.lights[0] == light);
    CHECK(data.lights[1] == lamp);
    const Json json = gameplayToJson(data);
    CHECK(json["lights"][0]["kind"] == "ceiling");
    CHECK(json["lights"][1]["room"].is_null());
    CHECK(json["lights"][1]["position"][2].get<double>() == 4.6);
}

TEST_CASE("Gameplay spawn points keep clear of furniture", "[gameplay]")
{
    Project p = gameplayProject();
    Floor& f = p.plan.floors[0];
    const Vec2 label = roomShape(f, *findRoom(f, "r1")).labelPoint;

    GameplayData data = dataOf(p);
    REQUIRE(roomOf(data, "r1").spawn.has_value());
    CHECK_THAT(roomOf(data, "r1").spawn->x, WithinAbs(label.x, 1e-9));
    CHECK_THAT(roomOf(data, "r1").spawn->y, WithinAbs(-label.y, 1e-9));

    Item table = test::buildItem("i1", "table", label.x, label.y, {1.0, 0.8, 0.75});
    Item filler = test::buildItem("i2", "block", 4.5, 2.0, {3.2, 4.2, 1.0});
    filler.room = "r2";
    f.items = {table, filler};
    data = dataOf(p);
    const auto spawn = roomOf(data, "r1").spawn;
    REQUIRE(spawn.has_value());
    CHECK(clearOfItem(*spawn, table));
    CHECK(spawn->x >= 0.15 + kSpawnClearance - 1e-9);
    CHECK(spawn->x <= 2.85 - kSpawnClearance + 1e-9);
    CHECK(spawn->z == 0.0);
    // Close to where the label was: just past the table's edge and the clearance.
    CHECK(std::hypot(spawn->x - label.x, -spawn->y - label.y) < 0.5 + kSpawnClearance + 0.1);
    CHECK_FALSE(roomOf(data, "r2").spawn.has_value());
}

TEST_CASE("The gameplay file matches its schema", "[gameplay][schema]")
{
    Project p = gameplayProject();
    GameplayLight light;
    light.id = "l1";
    light.floor = "f1";
    light.room = "r1";
    light.position = {1.0, -1.0, 2.9};
    const Json json = gameplayToJson(dataOf(p, {light}));
    CHECK(json["format"] == "plynth-gameplay");
    CHECK(json["version"] == 1);
    CHECK(json["units"] == "metres");
    CHECK(json["up"] == "Z");
    CHECK(json["project"] == "Flat");
    CHECK(json["doors"][1]["rooms"][1].is_null());
    CHECK(json["rooms"][1]["type"].is_null());
    CHECK(test::schemaProblem("gameplay.schema.json", nlohmann::json::parse(json.dump())) == "");
}
