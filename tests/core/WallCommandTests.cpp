#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/Snapping.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/SettingsCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Approx;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// A project with one floor: a 4 by 3 rectangle p1..p4 with walls w1..w4, a room r1 on all four walls, and
// a window o1 on w1 centred 3 m along it.
Project rectangleProject()
{
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 3.0, 0.0}, {"p4", 0.0, 3.0, 0.0}};
    const char* ends[4][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p1"}};
    for (int i = 0; i < 4; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.3;
        w.materialInside = "plaster";
        f.walls.push_back(w);
    }
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    f.rooms = {r};
    Opening o;
    o.id = "o1";
    o.wall = "w1";
    o.offset = 3.0;
    o.width = 0.8;
    o.type = OpeningType::Window;
    f.openings = {o};
    return p;
}

// Applies a command, then checks it succeeded and left a valid plan.
void applyOk(Command& c, Project& p)
{
    plynth::test::requireOk(c.apply(p));
    plynth::test::requirePlanValid(p.plan);
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

TEST_CASE("SetScaleCommand moves marks only when asked", "[core][command][scale]") {
    Project p = rectangleProject();
    p.plan.floors[0].points[1].cornerRadius = 0.1;
    p.plan.floors[0].walls[1].bulge = 0.4;
    p.plan.floors[0].walls[1].height = 2.5;
    p.plan.floors[0].openings[0].sill = 0.9;

    SECTION("the first scale moves nothing") {
        SetScaleCommand c(100.0);
        const Project before = p;
        applyOk(c, p);
        REQUIRE_FALSE(c.movedMarks());
        REQUIRE(c.changes() == Change::Scale);
        REQUIRE(p.plan.floors == before.plan.floors);
        REQUIRE(p.plan.scale.pixelsPerMetre == 100.0);
    }
    SECTION("clearing the scale moves nothing") {
        p.plan.scale.pixelsPerMetre = 100.0;
        SetScaleCommand c(std::nullopt);
        const Project before = p;
        applyOk(c, p);
        REQUIRE_FALSE(c.movedMarks());
        REQUIRE(p.plan.floors == before.plan.floors);
    }
    SECTION("a change with image positions rescales positions, bulge and offsets") {
        p.plan.scale.pixelsPerMetre = 100.0;
        SetScaleCommand c(50.0);
        const Project before = p;
        applyOk(c, p);
        REQUIRE(c.movedMarks());
        REQUIRE(c.changes() == (Change::Scale | Change::Points | Change::Walls | Change::Openings | Change::Items | Change::Stairs | Change::Roofs));
        const Floor& f = p.plan.floors[0];
        REQUIRE(f.points[1].x == Approx(8.0));
        REQUIRE(f.points[2].y == Approx(6.0));
        REQUIRE(f.walls[1].bulge == Approx(0.8));
        REQUIRE(f.openings[0].offset == Approx(6.0));
        REQUIRE(f.walls[1].thickness == 0.3);
        REQUIRE(f.walls[1].height == 2.5);
        REQUIRE(f.points[1].cornerRadius == 0.1);
        REQUIRE(f.openings[0].width == 0.8);
        REQUIRE(f.openings[0].sill == 0.9);
        c.undo(p);
        REQUIRE(p == before);
    }
    SECTION("a smaller factor shrinks the metre values") {
        p.plan.scale.pixelsPerMetre = 50.0;
        SetScaleCommand c(100.0, ScaleKeep::ImagePositions);
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].points[1].x == Approx(2.0));
        REQUIRE(p.plan.floors[0].openings[0].offset == Approx(1.5));
    }
    SECTION("keeping metres moves nothing") {
        p.plan.scale.pixelsPerMetre = 100.0;
        SetScaleCommand c(50.0, ScaleKeep::Metres);
        const Project before = p;
        applyOk(c, p);
        REQUIRE_FALSE(c.movedMarks());
        REQUIRE(c.changes() == Change::Scale);
        REQUIRE(p.plan.floors == before.plan.floors);
        REQUIRE(p.plan.scale.pixelsPerMetre == 50.0);
    }
    SECTION("the same scale moves nothing") {
        p.plan.scale.pixelsPerMetre = 100.0;
        SetScaleCommand c(100.0);
        applyOk(c, p);
        REQUIRE_FALSE(c.movedMarks());
    }
    SECTION("a plan without marks reports no move") {
        Project empty = makeNewProject();
        empty.plan.scale.pixelsPerMetre = 100.0;
        SetScaleCommand c(50.0);
        applyOk(c, empty);
        REQUIRE_FALSE(c.movedMarks());
    }
    SECTION("undo and redo of a move") {
        p.plan.scale.pixelsPerMetre = 100.0;
        SetScaleCommand c(25.0);
        roundTrip(c, p);
        REQUIRE(c.movedMarks());
    }
    SECTION("refusals leave the plan alone") {
        p.plan.scale.pixelsPerMetre = 100.0;
        SetScaleCommand c(0.0);
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("SetGridSizeCommand", "[core][command][settings]") {
    Project p = makeNewProject();
    SECTION("sets, undoes and redoes") {
        SetGridSizeCommand c(0.25);
        roundTrip(c, p);
        REQUIRE(p.settings.gridSize == 0.25);
        REQUIRE(c.changes() == Change::Settings);
        REQUIRE(c.floorId().empty());
    }
    SECTION("refusals") {
        for (double bad : {0.0, -0.5, kNaN, std::numeric_limits<double>::infinity()}) {
            SetGridSizeCommand c(bad);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
    }
}

TEST_CASE("SetWallThicknessCommand", "[core][command][wall]") {
    Project p = rectangleProject();
    SECTION("sets, undoes and redoes") {
        SetWallThicknessCommand c("w2", 0.45);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].walls[1].thickness == 0.45);
        REQUIRE(c.changes() == Change::Walls);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("refusals") {
        SetWallThicknessCommand unknown("w9", 0.2);
        applyRefused(unknown, p, ErrorCode::NotFound);
        for (double bad : {0.0, -1.0, kNaN}) {
            SetWallThicknessCommand c("w1", bad);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
    }
    SECTION("merges only with the same non zero key and wall") {
        SetWallThicknessCommand first("w1", 0.4, 7);
        applyOk(first, p);
        SetWallThicknessCommand sameKey("w1", 0.5, 7);
        applyOk(sameKey, p);
        REQUIRE(first.mergeWith(sameKey));
        first.undo(p);
        REQUIRE(p.plan.floors[0].walls[0].thickness == 0.3);
        applyOk(first, p);
        REQUIRE(p.plan.floors[0].walls[0].thickness == 0.5);

        SetWallThicknessCommand otherKey("w1", 0.6, 8);
        SetWallThicknessCommand otherWall("w2", 0.6, 7);
        SetWallThicknessCommand noKeyA("w1", 0.6);
        SetWallThicknessCommand noKeyB("w1", 0.7);
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(first.mergeWith(otherWall));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
        MovePointCommand move("p1", 1.0, 1.0, 7);
        REQUIRE_FALSE(first.mergeWith(move));
    }
}

TEST_CASE("SplitWallCommand", "[core][command][wall]") {
    Project p = rectangleProject();
    SECTION("splits geometry and keeps the old id on the first part") {
        SplitWallCommand c("w4", 1.0);
        applyOk(c, p);
        const Floor& f = p.plan.floors[0];
        REQUIRE(c.newPointId() == "p5");
        REQUIRE(c.newWallId() == "w5");
        const Wall* first = findWall(f, "w4");
        const Wall* second = findWall(f, "w5");
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(first->from == "p4");
        REQUIRE(first->to == "p5");
        REQUIRE(second->from == "p5");
        REQUIRE(second->to == "p1");
        REQUIRE(second->thickness == 0.3);
        REQUIRE(second->materialInside == "plaster");
        const Point* mid = findPoint(f, "p5");
        REQUIRE(mid);
        REQUIRE(mid->x == Approx(0.0));
        REQUIRE(mid->y == Approx(2.0));
        REQUIRE(c.changes() == (Change::Points | Change::Walls | Change::Rooms | Change::Openings | Change::Items));
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("rooms list the new wall right after the old one") {
        SplitWallCommand c("w2", 1.0);
        applyOk(c, p);
        const std::vector<Id> expected{"w1", "w2", "w5", "w3", "w4"};
        REQUIRE(p.plan.floors[0].rooms[0].walls == expected);
    }
    SECTION("a ring that runs the wall backwards gets the new wall before it") {
        Project q = rectangleProject();
        q.plan.floors[0].rooms[0].walls = {"w1", "w2", "w3", "w4"};
        q.plan.floors[0].walls[2].from = "p4";
        q.plan.floors[0].walls[2].to = "p3";
        SplitWallCommand c("w3", 1.0);
        applyOk(c, q);
        const std::vector<Id> expected{"w1", "w2", "w5", "w3", "w4"};
        REQUIRE(q.plan.floors[0].rooms[0].walls == expected);
    }
    SECTION("openings past the split move with a reduced offset") {
        SplitWallCommand c("w1", 2.0);
        applyOk(c, p);
        const Opening& o = p.plan.floors[0].openings[0];
        REQUIRE(o.wall == "w5");
        REQUIRE(o.offset == Approx(1.0));
    }
    SECTION("openings before the split stay") {
        SplitWallCommand c("w1", 3.8);
        applyOk(c, p);
        const Opening& o = p.plan.floors[0].openings[0];
        REQUIRE(o.wall == "w1");
        REQUIRE(o.offset == Approx(3.0));
    }
    SECTION("undo and redo keep the same ids") {
        SplitWallCommand c("w3", 1.5);
        roundTrip(c, p);
        REQUIRE(c.newWallId() == "w5");
    }
    SECTION("refusals") {
        SplitWallCommand unknown("w9", 1.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
        SplitWallCommand atStart("w1", 0.0);
        applyRefused(atStart, p, ErrorCode::InvalidValue);
        SplitWallCommand nearStart("w1", 0.0005);
        applyRefused(nearStart, p, ErrorCode::InvalidValue);
        SplitWallCommand atEnd("w1", 4.0);
        applyRefused(atEnd, p, ErrorCode::InvalidValue);
        SplitWallCommand past("w1", 5.0);
        applyRefused(past, p, ErrorCode::InvalidValue);
        SplitWallCommand nan("w1", kNaN);
        applyRefused(nan, p, ErrorCode::InvalidValue);
        SplitWallCommand covered("w1", 3.2);
        applyRefused(covered, p, ErrorCode::InUse);
    }
}

TEST_CASE("wallEndFromSnap", "[core][command][wall]") {
    SnapResult point;
    point.kind = SnapKind::Point;
    point.pointId = "p2";
    REQUIRE(wallEndFromSnap(point).kind == WallEnd::Kind::Point);
    REQUIRE(wallEndFromSnap(point).id == "p2");

    SnapResult wall;
    wall.kind = SnapKind::Wall;
    wall.wallId = "w1";
    wall.wallOffset = 1.5;
    const WallEnd onWall = wallEndFromSnap(wall);
    REQUIRE(onWall.kind == WallEnd::Kind::OnWall);
    REQUIRE(onWall.id == "w1");
    REQUIRE(onWall.offset == 1.5);

    for (SnapKind kind : {SnapKind::Grid, SnapKind::None}) {
        SnapResult free;
        free.kind = kind;
        free.position = {1.0, 2.0};
        const WallEnd e = wallEndFromSnap(free);
        REQUIRE(e.kind == WallEnd::Kind::NewPoint);
        REQUIRE(e.position == Vec2{1.0, 2.0});
    }
}

TEST_CASE("DrawWallCommand with each pair of ends", "[core][command][wall]") {
    Project p = rectangleProject();
    const std::size_t points = p.plan.floors[0].points.size();
    const std::size_t walls = p.plan.floors[0].walls.size();

    SECTION("point to point") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::atPoint("p3"), 0.1, 2.4);
        roundTrip(c, p);
        REQUIRE(c.wallId() == "w5");
        REQUIRE(c.fromPointId() == "p1");
        REQUIRE(c.toPointId() == "p3");
        REQUIRE(c.newPointIds().empty());
        REQUIRE(c.splitWallIds().empty());
        const Wall* w = findWall(p.plan.floors[0], "w5");
        REQUIRE(w);
        REQUIRE(w->thickness == 0.1);
        REQUIRE(w->height == 2.4);
        REQUIRE(c.name() == "Draw wall");
        REQUIRE(c.floorId() == "f1");
        REQUIRE(c.changes() == (Change::Points | Change::Walls | Change::Rooms | Change::Openings | Change::Items));
    }
    SECTION("point to new point") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::newPoint({-2.0, 0.0}));
        roundTrip(c, p);
        REQUIRE(c.toPointId() == "p5");
        REQUIRE(c.newPointIds() == std::vector<Id>{"p5"});
        REQUIRE(p.plan.floors[0].points.size() == points + 1);
        REQUIRE(p.plan.floors[0].walls.size() == walls + 1);
    }
    SECTION("new point to point") {
        DrawWallCommand c("f1", WallEnd::newPoint({6.0, 1.0}), WallEnd::atPoint("p2"));
        roundTrip(c, p);
        REQUIRE(c.fromPointId() == "p5");
        REQUIRE(c.toPointId() == "p2");
    }
    SECTION("new point to new point") {
        DrawWallCommand c("f1", WallEnd::newPoint({6.0, 1.0}), WallEnd::newPoint({8.0, 1.0}));
        roundTrip(c, p);
        REQUIRE(c.newPointIds() == std::vector<Id>{"p5", "p6"});
        REQUIRE(p.plan.floors[0].points.size() == points + 2);
    }
    SECTION("point to a place on a wall") {
        DrawWallCommand c("f1", WallEnd::atPoint("p4"), WallEnd::onWall("w1", 1.0));
        roundTrip(c, p);
        REQUIRE(c.splitWallIds() == std::vector<Id>{"w1"});
        REQUIRE(c.newPointIds() == std::vector<Id>{"p5"});
        REQUIRE(c.toPointId() == "p5");
        REQUIRE(p.plan.floors[0].walls.size() == walls + 2);
        const Wall* w = findWall(p.plan.floors[0], "w1");
        REQUIRE(w->to == "p5");
    }
    SECTION("a place on a wall to a new point") {
        DrawWallCommand c("f1", WallEnd::onWall("w3", 1.0), WallEnd::newPoint({3.0, 6.0}));
        roundTrip(c, p);
        REQUIRE(c.fromPointId() == "p5");
        REQUIRE(c.toPointId() == "p6");
        REQUIRE(c.splitWallIds() == std::vector<Id>{"w3"});
    }
    SECTION("a place on one wall to a place on another, across the room") {
        DrawWallCommand c("f1", WallEnd::onWall("w1", 2.0), WallEnd::onWall("w3", 2.0), 0.3);
        roundTrip(c, p);
        REQUIRE(c.splitWallIds() == std::vector<Id>{"w1", "w3"});
        REQUIRE(c.newPointIds().size() == 2);
        REQUIRE(p.plan.floors[0].points.size() == points + 2);
        REQUIRE(p.plan.floors[0].walls.size() == walls + 3);
        const std::vector<Id> expected{"w1", "w5", "w2", "w3", "w6", "w4"};
        REQUIRE(p.plan.floors[0].rooms[0].walls == expected);
    }
    SECTION("closing a loop onto an existing point") {
        DrawWallCommand first("f1", WallEnd::atPoint("p2"), WallEnd::newPoint({6.0, 0.0}));
        applyOk(first, p);
        DrawWallCommand second("f1", WallEnd::atPoint(first.toPointId()), WallEnd::newPoint({6.0, 2.0}));
        applyOk(second, p);
        DrawWallCommand close("f1", WallEnd::atPoint(second.toPointId()), WallEnd::atPoint("p3"));
        roundTrip(close, p);
        REQUIRE(close.toPointId() == "p3");
        REQUIRE(close.newPointIds().empty());
    }
    SECTION("a snapped click gives the same wall as the explicit ends") {
        Project q = p;
        SnapOptions o;
        o.radius = 0.1;
        const Floor& f = p.plan.floors[0];
        const SnapResult a = snapPosition(f, {2.0, 0.05}, o);
        const SnapResult b = snapPosition(f, {4.0, 3.02}, o);
        DrawWallCommand viaSnap("f1", wallEndFromSnap(a), wallEndFromSnap(b));
        DrawWallCommand direct("f1", WallEnd::onWall("w1", 2.0), WallEnd::atPoint("p3"));
        applyOk(viaSnap, p);
        applyOk(direct, q);
        REQUIRE(p == q);
    }
}

TEST_CASE("DrawWallCommand refusals and undo", "[core][command][wall]") {
    Project p = rectangleProject();
    SECTION("an existing wall between the ends") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::atPoint("p2"));
        applyRefused(c, p, ErrorCode::AlreadyExists);
        DrawWallCommand reversed("f1", WallEnd::atPoint("p2"), WallEnd::atPoint("p1"));
        applyRefused(reversed, p, ErrorCode::AlreadyExists);
    }
    SECTION("a wall along the piece it splits is refused and the split is rolled back") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::onWall("w1", 0.5));
        applyRefused(c, p, ErrorCode::AlreadyExists);
    }
    SECTION("the same point at both ends") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::atPoint("p1"));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("the same wall at both ends") {
        DrawWallCommand c("f1", WallEnd::onWall("w1", 1.0), WallEnd::onWall("w1", 2.0));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("no length") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::newPoint({0.0, 0.0}));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("unknown floor, point and wall") {
        DrawWallCommand floor("f9", WallEnd::atPoint("p1"), WallEnd::atPoint("p2"));
        applyRefused(floor, p, ErrorCode::NotFound);
        DrawWallCommand point("f1", WallEnd::atPoint("p9"), WallEnd::atPoint("p2"));
        applyRefused(point, p, ErrorCode::NotFound);
        DrawWallCommand wall("f1", WallEnd::atPoint("p1"), WallEnd::onWall("w9", 1.0));
        applyRefused(wall, p, ErrorCode::NotFound);
    }
    SECTION("an end on another floor") {
        Floor upstairs;
        upstairs.id = "f2";
        upstairs.name = "Up";
        upstairs.points = {{"p20", 0.0, 0.0, 0.0}};
        p.plan.floors.push_back(upstairs);
        DrawWallCommand c("f1", WallEnd::atPoint("p20"), WallEnd::atPoint("p2"));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("bad numbers") {
        DrawWallCommand nanPosition("f1", WallEnd::atPoint("p1"), WallEnd::newPoint({kNaN, 1.0}));
        applyRefused(nanPosition, p, ErrorCode::InvalidValue);
        DrawWallCommand thickness("f1", WallEnd::atPoint("p1"), WallEnd::newPoint({9.0, 9.0}), 0.0);
        applyRefused(thickness, p, ErrorCode::InvalidValue);
        DrawWallCommand height("f1", WallEnd::atPoint("p1"), WallEnd::newPoint({9.0, 9.0}), 0.2, -1.0);
        applyRefused(height, p, ErrorCode::InvalidValue);
        DrawWallCommand offset("f1", WallEnd::atPoint("p1"), WallEnd::onWall("w2", 99.0));
        applyRefused(offset, p, ErrorCode::InvalidValue);
    }
    SECTION("a failed second end undoes the first") {
        DrawWallCommand c("f1", WallEnd::onWall("w3", 1.0), WallEnd::onWall("w2", 0.0));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("a split blocked by an opening is refused whole") {
        DrawWallCommand c("f1", WallEnd::newPoint({6.0, 6.0}), WallEnd::onWall("w1", 3.1));
        applyRefused(c, p, ErrorCode::InUse);
    }
    SECTION("chained drawing is one undo step per wall") {
        const Project start = p;
        DrawWallCommand a("f1", WallEnd::newPoint({6.0, 0.0}), WallEnd::newPoint({6.0, 2.0}));
        applyOk(a, p);
        const Project afterA = p;
        DrawWallCommand b("f1", WallEnd::atPoint(a.toPointId()), WallEnd::newPoint({8.0, 2.0}));
        applyOk(b, p);
        b.undo(p);
        REQUIRE(p == afterA);
        a.undo(p);
        REQUIRE(p == start);
    }
}
