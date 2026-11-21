#include "TestSupport.h"

#include <plynth/core/Snapping.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using Catch::Approx;

namespace {

// A floor with one wall w1 from p1 (0, 0) to p2 (4, 0) and a loose point p3 at (2, 3).
Floor sampleFloor()
{
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 2.0, 3.0, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    w.thickness = 0.4;
    f.walls = {w};
    return f;
}

}

TEST_CASE("snapPosition snaps to a point first", "[core][snap]") {
    const Floor f = sampleFloor();
    SnapOptions o;
    o.radius = 0.3;
    o.gridSize = 0.5;
    const SnapResult r = snapPosition(f, {2.1, 2.9}, o);
    REQUIRE(r.kind == SnapKind::Point);
    REQUIRE(r.pointId == "p3");
    REQUIRE(r.position == Vec2{2.0, 3.0});
}

TEST_CASE("snapPosition: point beats wall beats grid", "[core][snap]") {
    Floor f = sampleFloor();
    SnapOptions o;
    o.radius = 0.3;
    o.gridSize = 0.5;
    SECTION("a point beats a wall near its end") {
        const SnapResult r = snapPosition(f, {0.1, 0.1}, o);
        REQUIRE(r.kind == SnapKind::Point);
        REQUIRE(r.pointId == "p1");
    }
    SECTION("a wall beats the grid") {
        const SnapResult r = snapPosition(f, {1.3, 0.2}, o);
        REQUIRE(r.kind == SnapKind::Wall);
        REQUIRE(r.wallId == "w1");
        REQUIRE(r.wallOffset == Approx(1.3));
        REQUIRE(r.position.x == Approx(1.3));
        REQUIRE(r.position.y == Approx(0.0));
    }
    SECTION("the grid when nothing is near") {
        const SnapResult r = snapPosition(f, {1.3, 1.8}, o);
        REQUIRE(r.kind == SnapKind::Grid);
        REQUIRE(r.position.x == Approx(1.5));
        REQUIRE(r.position.y == Approx(2.0));
    }
    SECTION("nothing at all when the grid is off") {
        o.gridSize = 0.0;
        const SnapResult r = snapPosition(f, {1.3, 1.8}, o);
        REQUIRE(r.kind == SnapKind::None);
        REQUIRE(r.position == Vec2{1.3, 1.8});
    }
    SECTION("radius 0 turns point and wall snapping off") {
        o.radius = 0.0;
        const SnapResult r = snapPosition(f, {0.1, 0.1}, o);
        REQUIRE(r.kind == SnapKind::Grid);
    }
    SECTION("switches for points and walls") {
        o.toPoints = false;
        REQUIRE(snapPosition(f, {2.1, 2.9}, o).kind == SnapKind::Grid);
        o.toWalls = false;
        REQUIRE(snapPosition(f, {1.3, 0.2}, o).kind == SnapKind::Grid);
    }
    SECTION("curved walls are not snapped to") {
        f.walls[0].bulge = 0.5;
        REQUIRE(snapPosition(f, {1.3, 0.2}, o).kind == SnapKind::Grid);
    }
}

TEST_CASE("snapPosition: the nearest of two candidates wins", "[core][snap]") {
    Floor f = sampleFloor();
    f.points.push_back({"p4", 2.2, 3.0, 0.0});
    SnapOptions o;
    o.radius = 0.5;
    REQUIRE(snapPosition(f, {2.15, 3.0}, o).pointId == "p4");
    REQUIRE(snapPosition(f, {2.05, 3.0}, o).pointId == "p3");
}

TEST_CASE("snapPosition with the grid", "[core][snap]") {
    Floor f;
    SnapOptions o;
    o.gridSize = 0.5;
    SECTION("negative coordinates round to the nearest node") {
        const SnapResult r = snapPosition(f, {-0.74, -0.76}, o);
        REQUIRE(r.position.x == Approx(-0.5));
        REQUIRE(r.position.y == Approx(-1.0));
    }
    SECTION("the grid starts at the image corner") {
        o.gridSize = 0.3;
        const SnapResult r = snapPosition(f, {0.44, 0.46}, o);
        REQUIRE(r.position.x == Approx(0.3));
        REQUIRE(r.position.y == Approx(0.6));
    }
    SECTION("negative or zero grid is off") {
        o.gridSize = -1.0;
        REQUIRE(snapPosition(f, {0.44, 0.46}, o).kind == SnapKind::None);
    }
}

TEST_CASE("snapPosition with orthogonalFrom", "[core][snap]") {
    Floor f = sampleFloor();
    SnapOptions o;
    o.orthogonalFrom = Vec2{1.0, 1.0};
    SECTION("picks the closer axis") {
        const SnapResult horizontal = snapPosition(f, {3.0, 1.2}, o);
        REQUIRE(horizontal.position == Vec2{3.0, 1.0});
        const SnapResult vertical = snapPosition(f, {1.2, 3.0}, o);
        REQUIRE(vertical.position == Vec2{1.0, 3.0});
        REQUIRE(vertical.kind == SnapKind::None);
    }
    SECTION("only the free coordinate is rounded") {
        o.gridSize = 0.5;
        const SnapResult r = snapPosition(f, {3.3, 1.2}, o);
        REQUIRE(r.kind == SnapKind::Grid);
        REQUIRE(r.position.x == Approx(3.5));
        REQUIRE(r.position.y == Approx(1.0));
    }
    SECTION("a point within the radius beats the orthogonal line") {
        o.radius = 0.3;
        const SnapResult r = snapPosition(f, {2.1, 2.9}, o);
        REQUIRE(r.kind == SnapKind::Point);
        REQUIRE(r.position == Vec2{2.0, 3.0});
    }
}

TEST_CASE("snapPosition ignorePoints", "[core][snap]") {
    const Floor f = sampleFloor();
    SnapOptions o;
    o.radius = 0.3;
    o.ignorePoints = {"p1"};
    SECTION("skips the point") {
        const SnapResult r = snapPosition(f, {0.1, 0.1}, o);
        REQUIRE(r.kind != SnapKind::Point);
    }
    SECTION("skips the walls that touch the point") {
        const SnapResult r = snapPosition(f, {1.3, 0.2}, o);
        REQUIRE(r.kind == SnapKind::None);
    }
    SECTION("other points still snap") {
        REQUIRE(snapPosition(f, {2.1, 2.9}, o).pointId == "p3");
    }
}

TEST_CASE("snapPosition leaves the very end of a wall to the point rule", "[core][snap]") {
    const Floor f = sampleFloor();
    SnapOptions o;
    o.radius = 0.3;
    o.toPoints = false;
    REQUIRE(snapPosition(f, {-0.1, 0.1}, o).kind == SnapKind::None);
}

TEST_CASE("pickAt", "[core][snap]") {
    const Floor f = sampleFloor();
    SECTION("a point beats a wall") {
        const Pick p = pickAt(f, {0.05, 0.05}, 0.1);
        REQUIRE(p.kind == PickKind::Point);
        REQUIRE(p.id == "p1");
    }
    SECTION("the wall band uses thickness") {
        REQUIRE(pickAt(f, {2.0, 0.25}, 0.1).kind == PickKind::Wall);
        REQUIRE(pickAt(f, {2.0, 0.25}, 0.1).id == "w1");
        REQUIRE(pickAt(f, {2.0, 0.35}, 0.1).kind == PickKind::None);
    }
    SECTION("nothing") {
        REQUIRE(pickAt(f, {10.0, 10.0}, 0.1) == Pick{});
    }
    SECTION("a negative radius counts as zero") {
        REQUIRE(pickAt(f, {2.0, 0.1}, -1.0).kind == PickKind::Wall);
    }
}
