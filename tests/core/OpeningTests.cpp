#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/OpeningShapes.h>
#include <plynth/core/Openings.h>
#include <plynth/core/Snapping.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/commands/OpeningCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Expects checkOpeningFit to refuse with a code and a word in the message.
void expectRefused(const Floor& f, const Opening& o, ErrorCode code, const std::string& word = {})
{
    const Status s = checkOpeningFit(f, o);
    INFO(s.message);
    REQUIRE(s.code == code);
    REQUIRE_FALSE(s.message.empty());
    if (!word.empty()) REQUIRE(s.message.find(word) != std::string::npos);
}

}

TEST_CASE("wallSpan: a wall between two perpendicular walls", "[core][openings][span]") {
    const Floor f = squareFloorWithWalls();
    const WallSpan span = wallSpan(f, f.walls[0]);
    REQUIRE(span.start == Approx(0.1));
    REQUIRE(span.end == Approx(3.9));
    REQUIRE(span.height == 3.0);
}

TEST_CASE("wallSpan: free ends, walls in line, rounded corners and T junctions", "[core][openings][span]") {
    SECTION("a free end has no clearance") {
        Floor f;
        f.points = {{"a", 0, 0, 0.0}, {"b", 4, 0, 0.0}};
        Wall w;
        w.id = "w1";
        w.from = "a";
        w.to = "b";
        f.walls = {w};
        const WallSpan span = wallSpan(f, f.walls[0]);
        REQUIRE(span.start == 0.0);
        REQUIRE(span.end == Approx(4.0));
    }
    SECTION("a wall continuing in line gives none at that end") {
        Floor f;
        f.points = {{"a", 0, 0, 0.0}, {"b", 4, 0, 0.0}, {"c", 8, 0.3, 0.0}};
        Wall w1;
        w1.id = "w1";
        w1.from = "a";
        w1.to = "b";
        Wall w2;
        w2.id = "w2";
        w2.from = "b";
        w2.to = "c";
        w2.thickness = 0.4;
        f.walls = {w1, w2};
        const WallSpan span = wallSpan(f, f.walls[0]);
        REQUIRE(span.end == Approx(4.0));
        REQUIRE(wallSpan(f, f.walls[1]).start == 0.0);
    }
    SECTION("a rounded corner of radius 1 gives 1") {
        Floor f = squareFloorWithWalls();
        findPoint(f, "p3")->cornerRadius = 1.0;
        REQUIRE(wallSpan(f, *findWall(f, "w2")).end == Approx(3.0));
        REQUIRE(wallSpan(f, *findWall(f, "w3")).start == Approx(1.0));
        REQUIRE(wallSpan(f, *findWall(f, "w1")).end == Approx(3.9));
    }
    SECTION("a 0.3 m wall meeting at a T gives 0.15") {
        Floor f;
        f.points = {{"a", 0, 0, 0.0}, {"b", 4, 0, 0.0}, {"c", 8, 0, 0.0}, {"d", 4, 3, 0.0}};
        const char* ends[3][2] = {{"a", "b"}, {"b", "c"}, {"b", "d"}};
        for (int i = 0; i < 3; ++i) {
            Wall w;
            w.id = "w" + std::to_string(i + 1);
            w.from = ends[i][0];
            w.to = ends[i][1];
            f.walls.push_back(w);
        }
        f.walls[2].thickness = 0.3;
        REQUIRE(wallSpan(f, f.walls[0]).end == Approx(3.85));
        REQUIRE(wallSpan(f, f.walls[1]).start == Approx(0.15));
        // The stem itself meets two walls across its end, each 0.2 m thick.
        REQUIRE(wallSpan(f, f.walls[2]).start == Approx(0.1));
    }
    SECTION("the thickest wall that does not continue in line counts") {
        Floor f = squareFloorWithWalls();
        findWall(f, "w2")->thickness = 0.6;
        REQUIRE(wallSpan(f, *findWall(f, "w1")).end == Approx(3.7));
    }
    SECTION("the wall's own height counts") {
        Floor f = squareFloorWithWalls();
        findWall(f, "w1")->height = 1.1;
        REQUIRE(wallSpan(f, *findWall(f, "w1")).height == 1.1);
    }
}

TEST_CASE("defaultOpening", "[core][openings]") {
    DefaultSizes sizes;
    sizes.doorWidth = 0.8;
    sizes.doorHeight = 2.0;
    sizes.windowWidth = 1.5;
    sizes.windowHeight = 1.1;
    sizes.windowSill = 0.7;

    const Opening door = defaultOpening(sizes, OpeningType::Door);
    REQUIRE(door.type == OpeningType::Door);
    REQUIRE(door.width == 0.8);
    REQUIRE(door.height == 2.0);
    REQUIRE(door.sill == 0.0);
    REQUIRE(door.shape == "rectangle");
    REQUIRE(door.hinge == HingeEnd::Start);
    REQUIRE(door.swing == WallSide::Right);
    REQUIRE_FALSE(door.frame);
    REQUIRE(door.id.empty());
    REQUIRE(door.wall.empty());

    const Opening window = defaultOpening(sizes, OpeningType::Window);
    REQUIRE(window.type == OpeningType::Window);
    REQUIRE(window.width == 1.5);
    REQUIRE(window.height == 1.1);
    REQUIRE(window.sill == 0.7);

    const Opening plain = defaultOpening(sizes, OpeningType::Plain);
    REQUIRE(plain.type == OpeningType::Plain);
    REQUIRE(plain.width == 0.8);
    REQUIRE(plain.height == 2.0);
    REQUIRE(plain.sill == 0.0);

    const Opening builtIn = defaultOpening(DefaultSizes{}, OpeningType::Window);
    REQUIRE(builtIn.width == 1.2);
    REQUIRE(builtIn.height == 1.2);
    REQUIRE(builtIn.sill == 0.9);
}

TEST_CASE("checkOpeningFit accepts a door in the middle of a wall", "[core][openings][fit]") {
    const Floor f = squareFloorWithWalls();
    REQUIRE(checkOpeningFit(f, doorOn("w1", 2.0)).ok());
    // Right up to the free part's edge is fine.
    REQUIRE(checkOpeningFit(f, doorOn("w1", 0.1 + 0.45)).ok());
    REQUIRE(checkOpeningFit(f, doorOn("w1", 3.9 - 0.45)).ok());
}

TEST_CASE("checkOpeningFit refusals, in order", "[core][openings][fit]") {
    Floor f = squareFloorWithWalls();
    Opening existing = doorOn("w1", 2.0);
    existing.id = "o1";
    f.openings = {existing};

    SECTION("an unknown wall") {
        expectRefused(f, doorOn("w9", 2.0), ErrorCode::NotFound, "w9");
    }
    SECTION("bad numbers") {
        Opening o = doorOn("w2", 2.0);
        o.width = 0.0;
        expectRefused(f, o, ErrorCode::InvalidValue);
        o = doorOn("w2", 2.0);
        o.height = -1.0;
        expectRefused(f, o, ErrorCode::InvalidValue);
        o = doorOn("w2", 2.0);
        o.sill = kNaN;
        expectRefused(f, o, ErrorCode::InvalidValue);
        o = doorOn("w2", 2.0);
        o.sill = -0.1;
        expectRefused(f, o, ErrorCode::InvalidValue);
        o = doorOn("w2", std::numeric_limits<double>::infinity());
        expectRefused(f, o, ErrorCode::InvalidValue);
        o = doorOn("w2", 2.0);
        o.width = kNaN;
        expectRefused(f, o, ErrorCode::InvalidValue);
    }
    SECTION("an unknown shape lists the shapes") {
        Opening o = doorOn("w2", 2.0);
        o.shape = "trapezoid";
        expectRefused(f, o, ErrorCode::InvalidValue, "round_arch");
    }
    SECTION("a round arch that is too low gives the shape's rule") {
        Opening o = doorOn("w2", 2.0);
        o.shape = "round_arch";
        o.width = 1.0;
        o.height = 0.4;
        expectRefused(f, o, ErrorCode::InvalidValue, "half the width");
    }
    SECTION("reaching past the free part says by how much") {
        Opening o = doorOn("w1", 0.3);
        expectRefused(f, o, ErrorCode::InvalidValue, "0.25 m");
        o = doorOn("w1", 3.8);
        expectRefused(f, o, ErrorCode::InvalidValue, "0.35 m");
        o = doorOn("w1", 0.1 + 0.45 - 0.01);
        expectRefused(f, o, ErrorCode::InvalidValue, "0.01 m");
        // Wider than the whole wall.
        o = doorOn("w3", 2.0);
        o.width = 3.9;
        expectRefused(f, o, ErrorCode::InvalidValue, "0.10 m");
    }
    SECTION("above the wall height") {
        Opening o = doorOn("w2", 2.0);
        o.sill = 1.0;
        expectRefused(f, o, ErrorCode::InvalidValue, "above the top");
        f.walls[1].height = 1.1;
        o = doorOn("w2", 2.0);
        expectRefused(f, o, ErrorCode::InvalidValue, "above the top");
        o.sill = 0.0;
        o.height = 1.1;
        REQUIRE(checkOpeningFit(f, o).ok());
    }
    SECTION("overlapping names the other opening") {
        expectRefused(f, doorOn("w1", 2.4), ErrorCode::InvalidValue, "o1");
        Opening window = doorOn("w1", 2.0);
        window.height = 1.0;
        window.sill = 0.5;
        expectRefused(f, window, ErrorCode::InvalidValue, "o1");
    }
    SECTION("a window above a door at the same offset is accepted") {
        Opening window = doorOn("w1", 2.0);
        window.type = OpeningType::Window;
        window.height = 0.8;
        window.sill = 2.1;
        REQUIRE(checkOpeningFit(f, window).ok());
    }
    SECTION("two touching doors are accepted") {
        REQUIRE(checkOpeningFit(f, doorOn("w1", 2.9)).ok());
        REQUIRE(checkOpeningFit(f, doorOn("w1", 1.1)).ok());
    }
    SECTION("the opening itself is not an obstacle") {
        Opening moved = existing;
        moved.offset = 2.2;
        REQUIRE(checkOpeningFit(f, moved).ok());
    }
    SECTION("an opening on another wall is not an obstacle") {
        REQUIRE(checkOpeningFit(f, doorOn("w3", 2.0)).ok());
    }
}

TEST_CASE("openingPlacement on a straight wall", "[core][openings][placement]") {
    Floor f = squareFloorWithWalls();
    Opening o = doorOn("w1", 2.0);
    o.width = 1.0;
    const OpeningPlacement p = openingPlacement(f, o);
    REQUIRE(p.valid);
    REQUIRE(p.centre.x == Approx(2.0));
    REQUIRE(p.start.x == Approx(1.5));
    REQUIRE(p.end.x == Approx(2.5));
    REQUIRE(p.start.y == Approx(0.0).margin(1e-12));
    REQUIRE(p.startTangent.x == Approx(1.0));
    REQUIRE(p.endTangent.x == Approx(1.0));
    REQUIRE(p.thickness == 0.2);
    REQUIRE(p.centreLine.size() == 2);
    REQUIRE(p.footprint.size() == 4);
    REQUIRE(signedArea(p.footprint) == Approx(1.0 * 0.2));
}

TEST_CASE("openingPlacement on a curved wall", "[core][openings][placement]") {
    Floor f = squareFloorWithWalls();
    f.walls[0].bulge = -2.0;
    Opening o = doorOn("w1", kPi);
    o.width = 1.0;
    const OpeningPlacement p = openingPlacement(f, o);
    REQUIRE(p.valid);
    const WallCurve curve = wallCurve(f, f.walls[0]);
    REQUIRE(projectOntoCurve(p.start, curve).offset == Approx(kPi - 0.5).margin(1e-9));
    REQUIRE(projectOntoCurve(p.end, curve).offset == Approx(kPi + 0.5).margin(1e-9));
    REQUIRE(distance(p.centre, curve.centre) == Approx(2.0).margin(1e-9));
    REQUIRE(distance(p.start, curve.centre) == Approx(2.0).margin(1e-9));
    REQUIRE(p.centreLine.size() >= 3);
    REQUIRE(p.centreLine.front() == p.start);
    REQUIRE(p.centreLine.back() == p.end);
    REQUIRE(std::hypot(p.startTangent.x, p.startTangent.y) == Approx(1.0));
    REQUIRE(signedArea(p.footprint) == Approx(1.0 * 0.2).epsilon(0.005));
    // The footprint stays within the wall's thickness of the arc.
    for (const Vec2& q : p.footprint) {
        REQUIRE(distance(q, curve.centre) == Approx(2.0).margin(0.11));
    }
}

TEST_CASE("openingPlacement clamps to the wall and handles a missing wall", "[core][openings][placement]") {
    const Floor f = squareFloorWithWalls();
    Opening o = doorOn("w1", 0.1);
    o.width = 1.0;
    const OpeningPlacement p = openingPlacement(f, o);
    REQUIRE(p.valid);
    REQUIRE(p.start == Vec2{0.0, 0.0});
    REQUIRE(p.end.x == Approx(0.6));
    REQUIRE_FALSE(openingPlacement(f, doorOn("w9", 1.0)).valid);
}

TEST_CASE("placeOpeningAt", "[core][openings][place]") {
    const Floor f = squareFloorWithWalls();
    SECTION("rounds to the step") {
        OpeningSpot s = placeOpeningAt(f, {2.02, 0.03}, 0.3, 0.9, 0.05);
        plynth::test::requireOk(s.status);
        REQUIRE(s.wall == "w1");
        REQUIRE(s.offset == Approx(2.0));
        s = placeOpeningAt(f, {2.03, 0.03}, 0.3, 0.9, 0.05);
        REQUIRE(s.offset == Approx(2.05));
        s = placeOpeningAt(f, {2.03, 0.03}, 0.3, 0.9, 0.0);
        REQUIRE(s.offset == Approx(2.03));
        s = placeOpeningAt(f, {2.03, 0.03}, 0.3, 0.9, 0.5);
        REQUIRE(s.offset == Approx(2.0));
    }
    SECTION("keeps the width inside the free part near a corner") {
        OpeningSpot s = placeOpeningAt(f, {0.2, 0.05}, 0.3, 0.9, 0.05);
        plynth::test::requireOk(s.status);
        REQUIRE(s.wall == "w1");
        REQUIRE(s.offset == Approx(0.1 + 0.45));
        s = placeOpeningAt(f, {3.6, 0.05}, 0.3, 0.9, 0.05);
        plynth::test::requireOk(s.status);
        REQUIRE(s.offset == Approx(3.9 - 0.45));
    }
    SECTION("picks the wall under the position and measures along it") {
        const OpeningSpot s = placeOpeningAt(f, {3.95, 1.5}, 0.3, 0.9, 0.0);
        plynth::test::requireOk(s.status);
        REQUIRE(s.wall == "w2");
        REQUIRE(s.offset == Approx(1.5));
    }
    SECTION("not found away from walls") {
        const OpeningSpot s = placeOpeningAt(f, {2.0, 2.0}, 0.3, 0.9, 0.05);
        REQUIRE(s.status.code == ErrorCode::NotFound);
        REQUIRE(s.wall.empty());
    }
    SECTION("a wall shorter than the width") {
        Floor g = squareFloorWithWalls();
        g.points.push_back({"p9", 0.5, 0.0, 0.0});
        g.points.push_back({"p10", 0.5, 0.6, 0.0});
        Wall w;
        w.id = "w9";
        w.from = "p9";
        w.to = "p10";
        g.walls.push_back(w);
        const OpeningSpot s = placeOpeningAt(g, {0.6, 0.4}, 0.05, 0.9, 0.05);
        REQUIRE(s.status.code == ErrorCode::InvalidValue);
        REQUIRE(s.status.message.find("0.") != std::string::npos);
    }
    SECTION("a bad width") {
        REQUIRE(placeOpeningAt(f, {2.0, 0.0}, 0.3, 0.0, 0.05).status.code == ErrorCode::InvalidValue);
        REQUIRE(placeOpeningAt(f, {2.0, 0.0}, 0.3, kNaN, 0.05).status.code == ErrorCode::InvalidValue);
    }
    SECTION("on a curved wall the offset is along the arc") {
        Floor g = f;
        g.walls[0].bulge = -2.0;
        const OpeningSpot s = placeOpeningAt(g, {2.0, -2.05}, 0.3, 0.9, 0.0);
        plynth::test::requireOk(s.status);
        REQUIRE(s.wall == "w1");
        REQUIRE(s.offset == Approx(kPi).epsilon(0.001));
    }
}

TEST_CASE("fitOpeningBox", "[core][openings][fit]") {
    const Floor f = squareFloorWithWalls();
    SECTION("clamps the offset into the free part") {
        REQUIRE(fitOpeningBox(f, doorOn("w1", -1.0)).offset == Approx(0.55));
        REQUIRE(fitOpeningBox(f, doorOn("w1", 10.0)).offset == Approx(3.45));
        REQUIRE(fitOpeningBox(f, doorOn("w1", 2.0)).offset == Approx(2.0));
    }
    SECTION("clamps the sill") {
        Opening o = doorOn("w1", 2.0);
        o.sill = 5.0;
        REQUIRE(fitOpeningBox(f, o).sill == Approx(0.9));
        o.sill = -1.0;
        REQUIRE(fitOpeningBox(f, o).sill == 0.0);
        o.sill = 0.4;
        REQUIRE(fitOpeningBox(f, o).sill == Approx(0.4));
    }
    SECTION("a too wide opening goes to the middle of the free part") {
        Opening o = doorOn("w1", 0.5);
        o.width = 4.0;
        REQUIRE(fitOpeningBox(f, o).offset == Approx(2.0));
        REQUIRE(fitOpeningBox(f, o).width == 4.0);
    }
    SECTION("an opening taller than the wall gets sill 0") {
        Opening o = doorOn("w1", 2.0);
        o.height = 3.5;
        o.sill = 0.5;
        REQUIRE(fitOpeningBox(f, o).sill == 0.0);
        REQUIRE(fitOpeningBox(f, o).height == 3.5);
    }
    SECTION("a missing wall changes nothing") {
        const Opening o = doorOn("w9", 7.0);
        REQUIRE(fitOpeningBox(f, o) == o);
    }
    SECTION("the result fits when it can") {
        Opening o = doorOn("w1", 99.0);
        o.sill = 9.0;
        REQUIRE(checkOpeningFit(f, fitOpeningBox(f, o)).ok());
    }
}

TEST_CASE("openingProblems", "[core][openings][problems]") {
    Project p = squareProjectWithWalls();
    AddOpeningCommand a(doorOn("w1", 3.0));
    plynth::test::requireOk(a.apply(p));
    AddOpeningCommand b(doorOn("w3", 2.0));
    plynth::test::requireOk(b.apply(p));
    REQUIRE(openingProblems(p.plan.floors[0]).empty());

    SECTION("a shorter wall lists the opening that no longer fits") {
        MovePointCommand move("p2", 2.0, 0.0);
        plynth::test::requireOk(move.apply(p));
        const auto problems = openingProblems(p.plan.floors[0]);
        REQUIRE(problems.size() == 1);
        REQUIRE(problems[0].opening == "o1");
        REQUIRE(problems[0].status.code == ErrorCode::InvalidValue);
        REQUIRE_FALSE(problems[0].status.message.empty());
        plynth::test::requirePlanValid(p.plan);
    }
    SECTION("bending keeps the offsets and may keep them fitting") {
        SetWallBulgeCommand bend("w1", 1.0);
        plynth::test::requireOk(bend.apply(p));
        REQUIRE(p.plan.floors[0].openings[0].offset == 3.0);
        REQUIRE(openingProblems(p.plan.floors[0]).empty());
    }
    SECTION("a rounded corner can flag an opening") {
        SetCornerRadiusCommand round("p3", 1.9);
        plynth::test::requireOk(round.apply(p));
        // w3 now has a clearance of 1.9 at its start and the door at offset 2.0 reaches into it.
        const auto problems = openingProblems(p.plan.floors[0]);
        REQUIRE(problems.size() == 1);
        REQUIRE(problems[0].opening == "o2");
    }
    SECTION("a problem list follows the floor's order") {
        MovePointCommand move("p4", 0.0, 1.0);
        plynth::test::requireOk(move.apply(p));
        MovePointCommand move2("p2", 1.0, 0.0);
        plynth::test::requireOk(move2.apply(p));
        const auto problems = openingProblems(p.plan.floors[0]);
        REQUIRE_FALSE(problems.empty());
        REQUIRE(problems[0].opening == "o1");
    }
}

TEST_CASE("pickAt finds openings before walls", "[core][openings][snap]") {
    Floor f = squareFloorWithWalls();
    Opening o = doorOn("w1", 2.0);
    o.id = "o1";
    f.openings = {o};
    // On the footprint.
    REQUIRE(pickAt(f, {2.0, 0.0}, 0.01) == Pick{PickKind::Opening, "o1"});
    REQUIRE(pickAt(f, {1.6, 0.05}, 0.01) == Pick{PickKind::Opening, "o1"});
    // Beside it on the wall.
    REQUIRE(pickAt(f, {1.0, 0.0}, 0.01) == Pick{PickKind::Wall, "w1"});
    REQUIRE(pickAt(f, {3.0, 0.0}, 0.01) == Pick{PickKind::Wall, "w1"});
    // Within the radius of the footprint.
    REQUIRE(pickAt(f, {2.0, 0.2}, 0.15) == Pick{PickKind::Opening, "o1"});
    // A point still wins.
    REQUIRE(pickAt(f, {0.0, 0.0}, 0.01) == Pick{PickKind::Point, "p1"});
    // An opening on a missing wall is skipped.
    f.openings.push_back(doorOn("w9", 1.0));
    f.openings.back().id = "o2";
    REQUIRE(pickAt(f, {1.0, 0.0}, 0.01) == Pick{PickKind::Wall, "w1"});
}

TEST_CASE("pickAt prefers the opening nearer the position", "[core][openings][snap]") {
    Floor f = squareFloorWithWalls();
    Opening a = doorOn("w1", 1.0);
    a.id = "o1";
    Opening b = doorOn("w1", 1.9);
    b.id = "o2";
    f.openings = {a, b};
    REQUIRE(pickAt(f, {1.4, 0.0}, 0.3) == Pick{PickKind::Opening, "o1"});
    REQUIRE(pickAt(f, {1.6, 0.0}, 0.3) == Pick{PickKind::Opening, "o2"});
}
