#include "FakeDetector.h"
#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/commands/DetectionCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;

namespace {

// The fixed proposals with rooms.
DetectionResult proposalsWithRooms()
{
    DetectionResult r = fixedProposals();
    proposeRooms(r);
    return r;
}

// Applies the command, then checks the plan is still valid.
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

}

TEST_CASE("ApplyDetectionCommand on an empty floor", "[core][command][detection]") {
    Project p = makeNewProject();
    ApplyDetectionCommand c("f1", proposalsWithRooms());
    REQUIRE(c.name() == "Accept detection");
    REQUIRE(c.wallIds().empty());
    const Project before = p;

    applyOk(c, p);
    const Project after = p;
    const Floor& f = p.plan.floors[0];
    REQUIRE(f.points.size() == 6);
    REQUIRE(f.walls.size() == 7);
    REQUIRE(f.rooms.size() == 2);
    REQUIRE(f.openings.empty());
    REQUIRE(c.wallIds() == std::vector<Id>{"w1", "w2", "w3", "w4", "w5", "w6", "w7"});
    REQUIRE(c.pointIds() == std::vector<Id>{"p1", "p2", "p3", "p4", "p5", "p6"});
    REQUIRE(c.roomIds() == std::vector<Id>{"r1", "r2"});
    REQUIRE(c.splitWallIds().empty());
    REQUIRE(c.skippedWalls() == 0);
    REQUIRE(c.skippedRooms() == 0);
    REQUIRE(c.floorId() == "f1");
    REQUIRE(c.changes() == (Change::Points | Change::Walls | Change::Rooms | Change::Openings));

    const Wall* across = findWall(f, "w7");
    REQUIRE(across);
    REQUIRE(across->thickness == 0.3);
    REQUIRE(findPoint(f, across->from)->x == 3.0);
    for (const Room& r : f.rooms) REQUIRE(r.walls.size() == 4);

    SECTION("undo gives an equal plan, redo gives the same ids") {
        c.undo(p);
        REQUIRE(p == before);
        applyOk(c, p);
        REQUIRE(p == after);
        REQUIRE(c.wallIds().size() == 7);
    }
    SECTION("applying twice in a row: the second is already_exists") {
        ApplyDetectionCommand again("f1", proposalsWithRooms());
        applyRefused(again, p, ErrorCode::AlreadyExists);
    }
}

TEST_CASE("ApplyDetectionCommand without rooms", "[core][command][detection]") {
    Project p = makeNewProject();
    ApplyDetectionCommand c("f1", proposalsWithRooms(), false);
    applyOk(c, p);
    REQUIRE(p.plan.floors[0].walls.size() == 7);
    REQUIRE(p.plan.floors[0].rooms.empty());
    REQUIRE(c.roomIds().empty());
    REQUIRE(c.skippedRooms() == 0);
}

TEST_CASE("ApplyDetectionCommand joins what is already drawn", "[core][command][detection]") {
    SECTION("a proposal end snaps onto an existing point") {
        Project p = makeNewProject();
        addPoint(p.plan.floors[0], "p1", 0.02, 0.01);
        ApplyDetectionCommand c("f1", proposalsWithRooms());
        const Project before = p;
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].points.size() == 6);
        REQUIRE(c.pointIds().size() == 5);
        REQUIRE(findPoint(p.plan.floors[0], "p1")->x == 0.02);
        const Wall* first = findWall(p.plan.floors[0], c.wallIds()[0]);
        REQUIRE(first->from == "p1");
        c.undo(p);
        REQUIRE(p == before);
    }
    SECTION("an end on an existing wall splits it") {
        Project p = makeNewProject();
        Floor& f = p.plan.floors[0];
        addPoint(f, "p1", 4, -2);
        addPoint(f, "p2", 4, 2);
        addWall(f, "w1", "p1", "p2", 0.2);
        DetectionResult r;
        r.points = {{0, 0}, {4, 0}};
        r.walls = {{0, 1, 0.2, ProposalState::New}};
        ApplyDetectionCommand c("f1", r);
        const Project before = p;
        applyOk(c, p);
        REQUIRE(c.splitWallIds() == std::vector<Id>{"w1"});
        REQUIRE(c.wallIds().size() == 1);
        REQUIRE(f.walls.size() == 3);
        REQUIRE(f.points.size() == 4);
        REQUIRE(c.pointIds().size() == 2);
        c.undo(p);
        REQUIRE(p == before);
        applyOk(c, p);
        REQUIRE(f.walls.size() == 3);
    }
    SECTION("Existing and Overlaps proposals are skipped") {
        Project p = makeNewProject();
        Floor& f = p.plan.floors[0];
        addPoint(f, "e1", 6, 0);
        addPoint(f, "e2", 6, 4);
        addWall(f, "e1w", "e1", "e2", 0.3);
        addPoint(f, "e3", 0, 0);
        addPoint(f, "e4", 1.5, 0);
        addWall(f, "e2w", "e3", "e4", 0.3);
        ApplyDetectionCommand c("f1", proposalsWithRooms());
        applyOk(c, p);
        REQUIRE(c.skippedWalls() == 2);
        REQUIRE(c.wallIds().size() == 5);
        REQUIRE(f.walls.size() == 7);
        REQUIRE(c.roomIds().size() == 1);
        REQUIRE(c.skippedRooms() == 1);
        REQUIRE(f.rooms.size() == 1);
    }
}

TEST_CASE("ApplyDetectionCommand refusals", "[core][command][detection]") {
    Project p = makeNewProject();

    SECTION("an unknown floor") {
        ApplyDetectionCommand c("f9", proposalsWithRooms());
        applyRefused(c, p, ErrorCode::NotFound);
    }
    SECTION("a cancelled result") {
        DetectionResult r = proposalsWithRooms();
        r.cancelled = true;
        ApplyDetectionCommand c("f1", r);
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("a result with an error status") {
        DetectionResult r = proposalsWithRooms();
        r.status = fail(ErrorCode::ImageError, "No image.");
        ApplyDetectionCommand c("f1", r);
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("a result that breaks the rules") {
        DetectionResult r = proposalsWithRooms();
        r.walls[0].to = 40;
        ApplyDetectionCommand c("f1", r);
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("nothing at all is already_exists") {
        ApplyDetectionCommand c("f1", DetectionResult{});
        applyRefused(c, p, ErrorCode::AlreadyExists);
    }
    SECTION("everything already drawn is already_exists") {
        ApplyDetectionCommand first("f1", proposalsWithRooms());
        applyOk(first, p);
        const Project drawn = p;
        ApplyDetectionCommand again("f1", proposalsWithRooms());
        applyRefused(again, p, ErrorCode::AlreadyExists);
        REQUIRE(p == drawn);
    }
}
