#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// Applies a command, then checks it succeeded and left a valid plan.
void applyOk(Command& c, Project& p)
{
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
}

// Applies a command that must be refused, and checks the project did not change.
void applyRefused(Command& c, Project& p, ErrorCode code)
{
    const Project before = p;
    const Status s = c.apply(p);
    INFO(s.message);
    REQUIRE(s.code == code);
    REQUIRE_FALSE(s.message.empty());
    REQUIRE(p == before);
}

// Applies, undoes and redoes a command, checking the project equals before after undo and the first result after redo.
void roundTrip(Command& c, Project& p)
{
    const Project before = p;
    applyOk(c, p);
    const Project after = p;
    REQUIRE(after != before);
    c.undo(p);
    REQUIRE(p == before);
    applyOk(c, p);
    REQUIRE(p == after);
}

}

TEST_CASE("AddRoomCommand", "[core][command][room]") {
    Project p = splitHouseProject();
    SECTION("lists the walls of the area and sets the type") {
        AddRoomCommand c("f1", {1.5, 2.0}, std::string("kitchen"));
        REQUIRE(c.roomId().empty());
        roundTrip(c, p);
        REQUIRE(c.roomId() == "r1");
        const Room* r = findRoom(p.plan.floors[0], "r1");
        REQUIRE(r);
        REQUIRE(r->type == "kitchen");
        const auto area = findClosedAreaAt(p.plan.floors[0], {1.5, 2.0});
        REQUIRE(area);
        REQUIRE(r->walls == area->walls);
        REQUIRE(c.changes() == Change::Rooms);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("works without a type") {
        AddRoomCommand c("f1", {4.5, 2.0});
        applyOk(c, p);
        REQUIRE_FALSE(p.plan.floors[0].rooms[0].type);
    }
    SECTION("a second room gets the next id") {
        AddRoomCommand a("f1", {1.5, 2.0});
        AddRoomCommand b("f1", {4.5, 2.0});
        applyOk(a, p);
        applyOk(b, p);
        REQUIRE(b.roomId() == "r2");
    }
    SECTION("refusals") {
        AddRoomCommand outside("f1", {20.0, 2.0});
        applyRefused(outside, p, ErrorCode::NotFound);
        AddRoomCommand unknownFloor("f9", {1.5, 2.0});
        applyRefused(unknownFloor, p, ErrorCode::NotFound);
        AddRoomCommand bad("f1", {1.5, 2.0}, std::string("not a name"));
        applyRefused(bad, p, ErrorCode::InvalidValue);
        AddRoomCommand empty("f1", {1.5, 2.0}, std::string(""));
        applyRefused(empty, p, ErrorCode::InvalidValue);
        AddRoomCommand nan("f1", {std::numeric_limits<double>::quiet_NaN(), 2.0});
        applyRefused(nan, p, ErrorCode::InvalidValue);
    }
    SECTION("inside an existing room names it") {
        AddRoomCommand first("f1", {1.5, 2.0});
        applyOk(first, p);
        AddRoomCommand again("f1", {2.0, 1.0});
        const Project before = p;
        const Status s = again.apply(p);
        REQUIRE(s.code == ErrorCode::AlreadyExists);
        REQUIRE(s.message.find("r1") != std::string::npos);
        REQUIRE(p == before);
    }
    SECTION("accepts a type from another catalog") {
        AddRoomCommand c("f1", {1.5, 2.0}, std::string("garage_2"));
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].rooms[0].type == "garage_2");
    }
}

TEST_CASE("AddRoomCommand on an island", "[core][command][room]") {
    Project p = islandProject();
    AddRoomCommand outer("f1", {0.5, 3.0}, std::string("living"));
    applyOk(outer, p);
    AddRoomCommand island("f1", {1.5, 1.5}, std::string("storage"));
    applyOk(island, p);
    REQUIRE(p.plan.floors[0].rooms[0].walls.size() == 8);
    REQUIRE(p.plan.floors[0].rooms[1].walls.size() == 4);
    REQUIRE(roomShape(p.plan.floors[0], p.plan.floors[0].rooms[0]).area == Approx(8.55).margin(1e-6));
}

TEST_CASE("RemoveRoomCommand", "[core][command][room]") {
    Project p = splitHouseProject();
    AddRoomCommand a("f1", {1.5, 2.0});
    AddRoomCommand b("f1", {4.5, 2.0});
    applyOk(a, p);
    applyOk(b, p);
    SECTION("removes and undo puts it back at the same place") {
        RemoveRoomCommand c("r1");
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].rooms.size() == 1);
        c.undo(p);
        REQUIRE(p.plan.floors[0].rooms[0].id == "r1");
        REQUIRE(p.plan.floors[0].rooms[1].id == "r2");
        REQUIRE(c.changes() == Change::Rooms);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("unknown room") {
        RemoveRoomCommand c("r9");
        applyRefused(c, p, ErrorCode::NotFound);
        RemoveRoomCommand notARoom("w1");
        applyRefused(notARoom, p, ErrorCode::NotFound);
    }
}

TEST_CASE("SetRoomTypeCommand", "[core][command][room]") {
    Project p = splitHouseProject();
    AddRoomCommand a("f1", {1.5, 2.0}, std::string("hall"));
    applyOk(a, p);
    SECTION("sets and undoes") {
        SetRoomTypeCommand c("r1", std::string("bedroom"));
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].rooms[0].type == "bedroom");
        REQUIRE(c.name() == "Set room type");
    }
    SECTION("clears and undoes") {
        SetRoomTypeCommand c("r1", std::nullopt);
        roundTrip(c, p);
        REQUIRE_FALSE(p.plan.floors[0].rooms[0].type);
        REQUIRE(c.name() == "Clear room type");
    }
    SECTION("refusals") {
        SetRoomTypeCommand unknown("r9", std::string("hall"));
        applyRefused(unknown, p, ErrorCode::NotFound);
        SetRoomTypeCommand bad("r1", std::string("two words"));
        applyRefused(bad, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("splitting a room wall keeps the room closed with the same area", "[core][command][room]") {
    Project p = splitHouseProject();
    AddRoomCommand a("f1", {1.5, 2.0});
    applyOk(a, p);
    const Floor& f = p.plan.floors[0];
    const RoomShape before = roomShape(f, f.rooms[0]);
    REQUIRE(before.closed);
    SplitWallCommand split("w1", 1.0);
    applyOk(split, p);
    REQUIRE(f.rooms[0].walls.size() == 5);
    const RoomShape after = roomShape(f, f.rooms[0]);
    REQUIRE(after.closed);
    REQUIRE(after.area == Approx(before.area).margin(1e-6));
    REQUIRE(after.area == Approx(2.7 * 3.7).margin(1e-6));
}
