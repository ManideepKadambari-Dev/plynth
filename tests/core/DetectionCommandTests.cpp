#include "FakeDetector.h"
#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/Openings.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/commands/DetectionCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

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
    REQUIRE(f.openings.size() == 1);
    REQUIRE(c.openingIds() == std::vector<Id>{"o1"});
    REQUIRE(c.skippedOpenings() == 0);
    REQUIRE(c.wallIds() == std::vector<Id>{"w1", "w2", "w3", "w4", "w5", "w6", "w7"});
    REQUIRE(c.pointIds() == std::vector<Id>{"p1", "p2", "p3", "p4", "p5", "p6"});
    REQUIRE(c.roomIds() == std::vector<Id>{"r1", "r2"});
    REQUIRE(c.splitWallIds().empty());
    REQUIRE(c.skippedWalls() == 0);
    REQUIRE(c.skippedRooms() == 0);
    REQUIRE(c.floorId() == "f1");
    REQUIRE(c.changes() == (Change::Points | Change::Walls | Change::Rooms | Change::Openings | Change::Items));

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

TEST_CASE("ApplyDetectionCommand adds openings", "[core][command][detection][opening]") {
    SECTION("the fake detector door gets its sizes from the project defaults") {
        Project p = makeNewProject();
        p.settings.defaults.doorHeight = 2.0;
        ApplyDetectionCommand c("f1", proposalsWithRooms());
        const Project before = p;
        applyOk(c, p);
        const Floor& f = p.plan.floors[0];
        REQUIRE(f.openings.size() == 1);
        const Opening& o = f.openings[0];
        REQUIRE(o.id == "o1");
        REQUIRE(o.wall == c.wallIds()[0]);
        REQUIRE(o.offset == 1.5);
        REQUIRE(o.type == OpeningType::Door);
        REQUIRE(o.shape == "rectangle");
        REQUIRE(o.width == 0.9);
        REQUIRE(o.height == 2.0);
        REQUIRE(o.sill == 0.0);
        REQUIRE(o.hinge == HingeEnd::Start);
        REQUIRE(o.swing == WallSide::Right);
        REQUIRE(openingProblems(f).empty());
        const Project after = p;
        c.undo(p);
        REQUIRE(p == before);
        applyOk(c, p);
        REQUIRE(p == after);
        REQUIRE(c.openingIds() == std::vector<Id>{"o1"});
    }
    SECTION("windows and plain openings use their own default sizes") {
        DetectionResult r = proposalsWithRooms();
        ProposedOpening window = r.openings[0];
        window.type = OpeningType::Window;
        window.offset = 2.4;
        window.width = 0.6;
        ProposedOpening plain = r.openings[0];
        plain.type = OpeningType::Plain;
        plain.wall = 4;
        plain.offset = 1.5;
        r.openings.push_back(window);
        r.openings.push_back(plain);
        Project p = makeNewProject();
        ApplyDetectionCommand c("f1", r);
        applyOk(c, p);
        const Floor& f = p.plan.floors[0];
        REQUIRE(f.openings.size() == 3);
        REQUIRE(f.openings[1].type == OpeningType::Window);
        REQUIRE(f.openings[1].height == 1.2);
        REQUIRE(f.openings[1].sill == 0.9);
        REQUIRE(f.openings[1].width == 0.6);
        REQUIRE(f.openings[2].type == OpeningType::Plain);
        REQUIRE(f.openings[2].height == 2.1);
        REQUIRE(f.openings[2].sill == 0.0);
    }
    SECTION("an opening on a wall drawn the other way gets hinge and swing turned round") {
        DetectionResult r = proposalsWithRooms();
        r.openings[0].offset = 1.0;
        r.openings[0].hinge = HingeEnd::End;
        r.openings[0].swing = WallSide::Left;
        Project p = makeNewProject();
        Floor& f = p.plan.floors[0];
        // The same wall as proposal wall 0, but running from (3, 0) to (0, 0).
        addPoint(f, "p1", 3.0, 0.0);
        addPoint(f, "p2", 0.0, 0.0);
        addWall(f, "w1", "p1", "p2", 0.3);
        ApplyDetectionCommand c("f1", r);
        applyOk(c, p);
        REQUIRE(c.openingIds().size() == 1);
        const Opening& o = f.openings[0];
        REQUIRE(o.wall == "w1");
        REQUIRE(o.offset == 2.0);
        REQUIRE(o.hinge == HingeEnd::Start);
        REQUIRE(o.swing == WallSide::Right);

        // On a wall running the same way they stay as they are.
        Project q = makeNewProject();
        addPoint(q.plan.floors[0], "p1", 0.0, 0.0);
        addPoint(q.plan.floors[0], "p2", 3.0, 0.0);
        addWall(q.plan.floors[0], "w1", "p1", "p2", 0.3);
        ApplyDetectionCommand same("f1", r);
        applyOk(same, q);
        const Opening& s = q.plan.floors[0].openings[0];
        REQUIRE(s.offset == 1.0);
        REQUIRE(s.hinge == HingeEnd::End);
        REQUIRE(s.swing == WallSide::Left);
    }
    SECTION("an opening already there is Existing and skipped, a new one beside it is added") {
        Project p = makeNewProject();
        DetectionResult r = proposalsWithRooms();
        ApplyDetectionCommand walls("f1", r);
        applyOk(walls, p);
        DetectionResult again = r;
        ProposedOpening window = again.openings[0];
        window.type = OpeningType::Window;
        window.offset = 2.5;
        window.width = 0.5;
        again.openings.push_back(window);
        ApplyDetectionCommand c("f1", again);
        applyOk(c, p);
        REQUIRE(c.skippedOpenings() == 1);
        REQUIRE(c.openingIds().size() == 1);
        REQUIRE(p.plan.floors[0].openings.size() == 2);
        REQUIRE(p.plan.floors[0].openings[1].type == OpeningType::Window);
    }
    SECTION("a door overhanging a junction by 0.02 m is moved into the free part") {
        DetectionResult r;
        r.points = {{0, 0}, {4, 0}, {0, 3}};
        r.walls = {{0, 1, 0.3, ProposalState::New}, {0, 2, 0.3, ProposalState::New}};
        ProposedOpening o;
        o.wall = 0;
        o.type = OpeningType::Door;
        o.offset = 0.45 + 0.15 - 0.02; // the left jamb is 0.02 m inside the wall at the corner
        o.width = 0.9;
        r.openings.push_back(o);
        Project p = makeNewProject();
        ApplyDetectionCommand c("f1", r);
        applyOk(c, p);
        REQUIRE(c.openingIds().size() == 1);
        REQUIRE(c.skippedOpenings() == 0);
        const Opening& placed = p.plan.floors[0].openings[0];
        REQUIRE(placed.width == 0.9);
        REQUIRE(placed.offset == Approx(0.6));
        REQUIRE(openingProblems(p.plan.floors[0]).empty());
    }
    SECTION("an opening wider than the free part is narrowed to it") {
        DetectionResult r;
        r.points = {{0, 0}, {1, 0}, {0, 3}, {1, 3}};
        r.walls = {{0, 1, 0.3, ProposalState::New}, {0, 2, 0.3, ProposalState::New}, {1, 3, 0.3, ProposalState::New}};
        ProposedOpening o;
        o.wall = 0;
        o.type = OpeningType::Plain;
        o.offset = 0.5;
        o.width = 0.72; // the free part is 1.0 - 0.3 = 0.7
        r.openings.push_back(o);
        Project p = makeNewProject();
        ApplyDetectionCommand c("f1", r);
        applyOk(c, p);
        REQUIRE(c.openingIds().size() == 1);
        const Opening& placed = p.plan.floors[0].openings[0];
        REQUIRE(placed.width == Approx(0.7));
        REQUIRE(placed.offset == Approx(0.5));
        REQUIRE(openingProblems(p.plan.floors[0]).empty());
    }
    SECTION("an opening that overhangs by more than the slack is skipped and counted") {
        DetectionResult r;
        r.points = {{0, 0}, {4, 0}, {0, 3}};
        r.walls = {{0, 1, 0.3, ProposalState::New}, {0, 2, 0.3, ProposalState::New}};
        ProposedOpening o;
        o.wall = 0;
        o.type = OpeningType::Door;
        o.offset = 0.5; // the left jamb is 0.05 m left of the centre line, 0.2 m inside the wall
        o.width = 0.9;
        r.openings.push_back(o);
        Project p = makeNewProject();
        ApplyDetectionCommand c("f1", r);
        applyOk(c, p);
        REQUIRE(c.openingIds().empty());
        REQUIRE(c.skippedOpenings() == 1);
        REQUIRE(p.plan.floors[0].openings.empty());
    }
    SECTION("without opening proposals none are added") {
        DetectionResult r = proposalsWithRooms();
        r.openings.clear();
        Project p = makeNewProject();
        ApplyDetectionCommand c("f1", r);
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].openings.empty());
        REQUIRE(c.openingIds().empty());
        REQUIRE(c.skippedOpenings() == 0);
    }
    SECTION("kDetectedOpeningSlack is 0.05 m") {
        REQUIRE(kDetectedOpeningSlack == 0.05);
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
