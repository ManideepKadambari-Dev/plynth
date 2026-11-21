#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/RoomCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;

namespace {

// The split house with a room on each side (r1 left, r2 right), a loose point p20 and a loose wall w20 from p6 to p21.
Project houseWithRooms()
{
    Project p = splitHouseProject();
    Floor& f = p.plan.floors[0];
    addPoint(f, "p20", 9, 9);
    addPoint(f, "p21", 3, 6);
    addWall(f, "w20", "p6", "p21", 0.2);
    AddRoomCommand a("f1", {1.5, 2.0});
    AddRoomCommand b("f1", {4.5, 2.0});
    requireOk(a.apply(p));
    requireOk(b.apply(p));
    return p;
}

// Builds a delete for the ids, runs it, checks the plan is valid, then undoes and redoes it.
DeleteSet runDelete(Project& p, const std::vector<Id>& ids)
{
    const Project before = p;
    DeleteBuild build = makeDeleteCommand(p.plan, ids);
    requireOk(build.status);
    REQUIRE(build.command);
    REQUIRE(build.command->name() == "Delete");
    requireOk(build.command->apply(p));
    requirePlanValid(p.plan);
    const Project after = p;
    REQUIRE(after != before);
    build.command->undo(p);
    REQUIRE(p == before);
    requireOk(build.command->apply(p));
    REQUIRE(p == after);
    return build.removes;
}

}

TEST_CASE("delete a point", "[core][command][delete]") {
    Project p = houseWithRooms();
    const DeleteSet gone = runDelete(p, {"p5"});
    REQUIRE(gone.rooms == std::vector<Id>{"r1", "r2"});
    REQUIRE(gone.walls == std::vector<Id>{"w1", "w2", "w7"});
    REQUIRE(gone.points == std::vector<Id>{"p5"});
    const Floor& f = p.plan.floors[0];
    REQUIRE(f.rooms.empty());
    REQUIRE(findPoint(f, "p5") == nullptr);
    REQUIRE(findPoint(f, "p20") != nullptr);
}

TEST_CASE("delete a point removes loose end points but keeps free points", "[core][command][delete]") {
    Project p = splitHouseProject();
    Floor& f = p.plan.floors[0];
    addPoint(f, "p20", 9, 9);
    addPoint(f, "p21", 3, 6);
    addWall(f, "w20", "p6", "p21", 0.2);
    const DeleteSet gone = runDelete(p, {"p6"});
    REQUIRE(std::find(gone.points.begin(), gone.points.end(), "p21") != gone.points.end());
    REQUIRE(std::find(gone.walls.begin(), gone.walls.end(), "w20") != gone.walls.end());
    REQUIRE(std::find(gone.points.begin(), gone.points.end(), "p20") == gone.points.end());
}

TEST_CASE("delete a wall", "[core][command][delete]") {
    Project p = houseWithRooms();
    SECTION("a room wall takes its rooms and leaves points still in use") {
        const DeleteSet gone = runDelete(p, {"w1"});
        REQUIRE(gone.rooms == std::vector<Id>{"r1"});
        REQUIRE(gone.walls == std::vector<Id>{"w1"});
        REQUIRE(gone.points.empty());
        REQUIRE(findPoint(p.plan.floors[0], "p5") != nullptr);
        REQUIRE(findPoint(p.plan.floors[0], "p1") != nullptr);
    }
    SECTION("a loose wall takes its loose end point") {
        const DeleteSet gone = runDelete(p, {"w20"});
        REQUIRE(gone.rooms.empty());
        REQUIRE(gone.walls == std::vector<Id>{"w20"});
        REQUIRE(gone.points == std::vector<Id>{"p21"});
    }
}

TEST_CASE("delete a room keeps its walls", "[core][command][delete]") {
    Project p = houseWithRooms();
    const DeleteSet gone = runDelete(p, {"r2"});
    REQUIRE(gone.rooms == std::vector<Id>{"r2"});
    REQUIRE(gone.walls.empty());
    REQUIRE(gone.points.empty());
    REQUIRE(p.plan.floors[0].walls.size() == 8);
}

TEST_CASE("delete several items and repeated ids", "[core][command][delete]") {
    Project p = houseWithRooms();
    const DeleteSet gone = runDelete(p, {"r1", "w20", "w20", "p20"});
    REQUIRE(gone.rooms == std::vector<Id>{"r1"});
    REQUIRE(gone.walls == std::vector<Id>{"w20"});
    REQUIRE(gone.points == std::vector<Id>{"p20", "p21"});
}

TEST_CASE("delete refusals", "[core][command][delete]") {
    Project p = houseWithRooms();
    const Project before = p;
    SECTION("an opening on a doomed wall") {
        Opening o;
        o.id = "o1";
        o.wall = "w7";
        o.offset = 2.0;
        p.plan.floors[0].openings = {o};
        const DeleteBuild viaPoint = makeDeleteCommand(p.plan, std::vector<Id>{"p5"});
        REQUIRE(viaPoint.status.code == ErrorCode::InUse);
        REQUIRE_FALSE(viaPoint.command);
        const DeleteBuild viaWall = makeDeleteCommand(p.plan, std::vector<Id>{"w7"});
        REQUIRE(viaWall.status.code == ErrorCode::InUse);
        const DeleteBuild viaRoom = makeDeleteCommand(p.plan, std::vector<Id>{"r1"});
        REQUIRE(viaRoom.status.ok());
    }
    SECTION("unknown ids and ids that are not points, walls or rooms") {
        REQUIRE(makeDeleteCommand(p.plan, std::vector<Id>{"p99"}).status.code == ErrorCode::NotFound);
        REQUIRE(makeDeleteCommand(p.plan, std::vector<Id>{"f1"}).status.code == ErrorCode::NotFound);
        REQUIRE(makeDeleteCommand(p.plan, std::vector<Id>{"w1", "nope"}).status.code == ErrorCode::NotFound);
    }
    SECTION("an empty list") {
        const DeleteBuild b = makeDeleteCommand(p.plan, std::vector<Id>{});
        REQUIRE(b.status.code == ErrorCode::InvalidValue);
        REQUIRE_FALSE(b.command);
    }
    SECTION("ids on different floors") {
        Floor up;
        up.id = "f2";
        up.name = "Up";
        up.points = {{"p50", 0.0, 0.0, 0.0}};
        p.plan.floors.push_back(up);
        REQUIRE(makeDeleteCommand(p.plan, std::vector<Id>{"p1", "p50"}).status.code == ErrorCode::InvalidValue);
    }
    REQUIRE(p.plan.floors[0].walls.size() == before.plan.floors[0].walls.size());
}
