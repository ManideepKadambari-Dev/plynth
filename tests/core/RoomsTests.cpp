#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Rooms.h>
#include <plynth/core/Snapping.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// The wall ids of an area as a set.
std::set<Id> wallSet(const ClosedArea& a)
{
    return std::set<Id>(a.walls.begin(), a.walls.end());
}

// Says whether a position is inside the first ring and outside the others.
bool insideRings(Vec2 position, const std::vector<std::vector<Vec2>>& rings)
{
    if (rings.empty() || !insidePolygon(position, rings[0])) return false;
    for (std::size_t i = 1; i < rings.size(); ++i) {
        if (insidePolygon(position, rings[i])) return false;
    }
    return true;
}

// A room that lists the walls of an area.
Room roomOf(const ClosedArea& a)
{
    Room r;
    r.id = "r1";
    r.walls = a.walls;
    return r;
}

}

TEST_CASE("findClosedAreas: one square", "[core][rooms]") {
    Project p = makeNewProject();
    addRectangle(p.plan.floors[0], 1, 0, 0, 4, 4, 0.2);
    const auto areas = findClosedAreas(p.plan.floors[0]);
    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].walls == std::vector<Id>{"w11", "w12", "w13", "w14"});
    REQUIRE(areas[0].centreLineArea == Approx(16.0));
    REQUIRE(areas[0].rings.size() == 1);
    REQUIRE(signedArea(areas[0].rings[0]) > 0.0);
    REQUIRE(insidePolygon(areas[0].inside, areas[0].rings[0]));
}

TEST_CASE("findClosedAreas: a T junction makes two areas and never the outside", "[core][rooms]") {
    const Project p = splitHouseProject();
    const auto areas = findClosedAreas(p.plan.floors[0]);
    REQUIRE(areas.size() == 2);
    const std::set<Id> left{"w1", "w5", "w6", "w7"};
    const std::set<Id> right{"w2", "w3", "w4", "w7"};
    REQUIRE(wallSet(areas[0]) == left);
    REQUIRE(wallSet(areas[1]) == right);
    REQUIRE(areas[0].centreLineArea == Approx(12.0));
    REQUIRE(areas[1].centreLineArea == Approx(12.0));
    REQUIRE(areas[0].inside.x < 3.0);
    REQUIRE(areas[1].inside.x > 3.0);
}

TEST_CASE("findClosedAreas: an island with no connection", "[core][rooms]") {
    const Project p = islandProject();
    const Floor& f = p.plan.floors[0];
    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 2);
    const ClosedArea& outer = areas[0];
    const ClosedArea& island = areas[1];
    REQUIRE(outer.walls.size() == 8);
    REQUIRE(outer.rings.size() == 2);
    REQUIRE(outer.centreLineArea == Approx(12.0 - 1.0));
    REQUIRE(island.walls == std::vector<Id>{"w5", "w6", "w7", "w8"});
    REQUIRE(island.centreLineArea == Approx(1.0));

    SECTION("the position decides which one holds it") {
        const auto inIsland = findClosedAreaAt(f, {1.5, 1.5});
        REQUIRE(inIsland);
        REQUIRE(inIsland->walls == island.walls);
        const auto between = findClosedAreaAt(f, {0.5, 3.0});
        REQUIRE(between);
        REQUIRE(between->walls == outer.walls);
        REQUIRE_FALSE(findClosedAreaAt(f, {10.0, 10.0}));
    }
    SECTION("inside lies in the right place") {
        REQUIRE(insideRings(outer.inside, outer.rings));
    }
}

TEST_CASE("findClosedAreas: an island joined by a bridge", "[core][rooms]") {
    Project p = islandProject();
    Floor& f = p.plan.floors[0];
    addWall(f, "w9", "p1", "p5", 0.2);
    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 2);
    for (const ClosedArea& a : areas) {
        REQUIRE(std::find(a.walls.begin(), a.walls.end(), "w9") == a.walls.end());
    }
    REQUIRE(areas[0].walls.size() == 8);
    REQUIRE(areas[0].centreLineArea == Approx(11.0));
}

TEST_CASE("findClosedAreas: a dangling wall stays out of the room", "[core][rooms]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addRectangle(f, 1, 0, 0, 4, 4, 0.2);
    addPoint(f, "p90", 2, 2);
    addWall(f, "w90", "p90", "p11", 0.2);
    addPoint(f, "p91", 2, 3);
    addWall(f, "w91", "p91", "p90", 0.2);
    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].walls.size() == 4);
    REQUIRE(areas[0].centreLineArea == Approx(16.0));
}

TEST_CASE("findClosedAreas: two separate buildings", "[core][rooms]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addRectangle(f, 1, 0, 0, 4, 4, 0.2);
    addRectangle(f, 2, 10, 0, 12, 3, 0.2);
    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 2);
    REQUIRE(areas[0].rings.size() == 1);
    REQUIRE(areas[1].rings.size() == 1);
    REQUIRE(areas[1].centreLineArea == Approx(6.0));
}

TEST_CASE("findClosedAreas: open shapes have no area", "[core][rooms]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addPoint(f, "p1", 0, 0);
    addPoint(f, "p2", 0, 3);
    addPoint(f, "p3", 4, 3);
    addPoint(f, "p4", 4, 0);
    addWall(f, "w1", "p1", "p2", 0.2);
    addWall(f, "w2", "p2", "p3", 0.2);
    addWall(f, "w3", "p3", "p4", 0.2);
    REQUIRE(findClosedAreas(f).empty());
    REQUIRE(findClosedAreas(Floor{}).empty());
}

TEST_CASE("findClosedAreas: an L shape", "[core][rooms]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    const double xs[6] = {0, 4, 4, 2, 2, 0};
    const double ys[6] = {0, 0, 2, 2, 4, 4};
    for (int i = 0; i < 6; ++i) addPoint(f, "p" + std::to_string(i + 1), xs[i], ys[i]);
    for (int i = 0; i < 6; ++i) {
        addWall(f, "w" + std::to_string(i + 1), "p" + std::to_string(i + 1), "p" + std::to_string((i + 1) % 6 + 1), 0.2);
    }
    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].centreLineArea == Approx(12.0));
    REQUIRE(insidePolygon(areas[0].inside, areas[0].rings[0]));
    const RoomShape shape = roomShape(f, roomOf(areas[0]));
    REQUIRE(shape.closed);
    REQUIRE(insideRings(shape.labelPoint, shape.floorArea));
}

TEST_CASE("findClosedAreas survives bad input", "[core][rooms]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addRectangle(f, 1, 0, 0, 4, 4, 0.2);
    addPoint(f, "p90", 0, 0);
    addWall(f, "w90", "p90", "p11", 0.2);
    addWall(f, "w91", "p11", "p11", 0.2);
    addWall(f, "w92", "p11", "p99", 0.2);
    addWall(f, "w93", "p12", "p11", 0.2);
    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].walls == std::vector<Id>{"w11", "w12", "w13", "w14"});
}

TEST_CASE("findClosedAreas gives the same result for shuffled walls", "[core][rooms]") {
    Project p = splitHouseProject();
    Floor& f = p.plan.floors[0];
    const auto first = findClosedAreas(f);
    std::reverse(f.walls.begin(), f.walls.end());
    std::reverse(f.points.begin(), f.points.end());
    const auto second = findClosedAreas(f);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].walls == second[i].walls);
        REQUIRE(first[i].centreLineArea == Approx(second[i].centreLineArea));
    }
    Project q = islandProject();
    const auto a = findClosedAreas(q.plan.floors[0]);
    std::rotate(q.plan.floors[0].walls.begin(), q.plan.floors[0].walls.begin() + 3, q.plan.floors[0].walls.end());
    const auto b = findClosedAreas(q.plan.floors[0]);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i].walls == b[i].walls);
}

TEST_CASE("roomShape: a square of 0.2 m walls", "[core][rooms]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addRectangle(f, 1, 0, 0, 4, 4, 0.2);
    Room r;
    r.id = "r1";
    r.walls = {"w11", "w12", "w13", "w14"};
    const RoomShape shape = roomShape(f, r);
    REQUIRE(shape.closed);
    REQUIRE(shape.area == Approx(14.44).epsilon(0).margin(1e-6));
    REQUIRE(shape.rings.size() == 1);
    REQUIRE_FALSE(shape.floorArea.empty());
    REQUIRE(signedArea(shape.floorArea[0]) > 0.0);
    REQUIRE(shape.labelPoint.x == Approx(2.0).margin(0.5));
    REQUIRE(shape.labelPoint.y == Approx(2.0).margin(0.5));
}

TEST_CASE("roomShape: two areas split by a wall", "[core][rooms]") {
    const Project p = splitHouseProject();
    const Floor& f = p.plan.floors[0];
    for (const ClosedArea& a : findClosedAreas(f)) {
        const RoomShape shape = roomShape(f, roomOf(a));
        REQUIRE(shape.closed);
        REQUIRE(shape.area == Approx(2.7 * 3.7).epsilon(0).margin(1e-6));
    }
}

TEST_CASE("roomShape: an island inside a room", "[core][rooms]") {
    const Project p = islandProject();
    const Floor& f = p.plan.floors[0];
    const auto areas = findClosedAreas(f);
    const RoomShape outer = roomShape(f, roomOf(areas[0]));
    REQUIRE(outer.closed);
    REQUIRE(outer.area == Approx(9.99 - 1.44).epsilon(0).margin(1e-6));
    REQUIRE(outer.rings.size() == 2);
    REQUIRE(insideRings(outer.labelPoint, outer.floorArea));
    const RoomShape island = roomShape(f, roomOf(areas[1]));
    REQUIRE(island.closed);
    REQUIRE(island.area == Approx(0.64).epsilon(0).margin(1e-6));
}

TEST_CASE("roomShape: rooms that do not close", "[core][rooms]") {
    const Project p = splitHouseProject();
    const Floor& f = p.plan.floors[0];
    Room missingWall;
    missingWall.walls = {"w1", "w5", "w6", "w99"};
    Room openRing;
    openRing.walls = {"w1", "w5", "w6"};
    Room empty;
    Room duplicate;
    duplicate.walls = {"w1", "w5", "w6", "w7", "w7"};
    for (const Room* r : {&missingWall, &openRing, &empty, &duplicate}) {
        const RoomShape shape = roomShape(f, *r);
        REQUIRE_FALSE(shape.closed);
        REQUIRE(shape.area == 0.0);
        REQUIRE(shape.floorArea.empty());
    }
}

TEST_CASE("roomAt and pickAt", "[core][rooms]") {
    Project p = splitHouseProject();
    Floor& f = p.plan.floors[0];
    Room left;
    left.id = "r1";
    left.walls = {"w1", "w5", "w6", "w7"};
    f.rooms = {left};
    REQUIRE(roomAt(f, {1.5, 2.0}) == "r1");
    REQUIRE(roomAt(f, {4.5, 2.0}).empty());
    REQUIRE(roomAt(f, {20.0, 2.0}).empty());
    const Pick room = pickAt(f, {1.5, 2.0}, 0.1);
    REQUIRE(room.kind == PickKind::Room);
    REQUIRE(room.id == "r1");
    REQUIRE(pickAt(f, {4.5, 2.0}, 0.1).kind == PickKind::None);
    REQUIRE(pickAt(f, {1.5, 0.0}, 0.1).kind == PickKind::Wall);
}

TEST_CASE("roomShape area stays exact on coordinates that are not round", "[core][rooms]") {
    Project p = splitHouseProject();
    Floor& f = p.plan.floors[0];
    for (Point& pt : f.points) {
        pt.x += 0.123456789;
        pt.y += 0.987654321;
    }
    for (const ClosedArea& a : findClosedAreas(f)) {
        REQUIRE(roomShape(f, roomOf(a)).area == Approx(2.7 * 3.7).epsilon(0).margin(1e-6));
    }
    Project q = islandProject();
    for (Point& pt : q.plan.floors[0].points) {
        pt.x += 0.1234567891;
        pt.y += 0.7654321987;
    }
    const auto areas = findClosedAreas(q.plan.floors[0]);
    REQUIRE(roomShape(q.plan.floors[0], roomOf(areas[0])).area == Approx(8.55).epsilon(0).margin(1e-6));
    REQUIRE(roomShape(q.plan.floors[0], roomOf(areas[1])).area == Approx(0.64).epsilon(0).margin(1e-6));
}
