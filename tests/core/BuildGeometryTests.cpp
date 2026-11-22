#include "SquareFixtures.h"
#include "TestSupport.h"
#include "../../core/src/BandOutline.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Openings.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/WallGeometry.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// The square with room r1 on its four walls.
Floor squareWithRoom()
{
    Floor f = squareFloorWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    f.rooms.push_back(r);
    return f;
}

// Signed area of a ring, positive when it runs anticlockwise seen from above.
double ringArea(const std::vector<Vec2>& ring)
{
    return signedArea(ring);
}

// A door on a wall with an id, 0.9 by 2.1.
Opening door(const std::string& wall, double offset)
{
    Opening o = doorOn(wall, offset);
    o.id = "o1";
    return o;
}

// The smallest and largest value of one coordinate.
std::pair<double, double> range(const std::vector<Vec2>& ring, bool x)
{
    double lo = 1e300;
    double hi = -1e300;
    for (const Vec2& p : ring) {
        lo = std::min(lo, x ? p.x : p.y);
        hi = std::max(hi, x ? p.x : p.y);
    }
    return {lo, hi};
}

// The ids of the walls named by the edges of a ring.
std::set<Id> wallsOf(const WallRing& ring)
{
    std::set<Id> out;
    for (const WallEdge& e : ring.edges) out.insert(e.wall);
    return out;
}

}

TEST_CASE("planToWorld flips y and floorElevations stacks the ceilings", "[core][build]") {
    const Vec2 w = planToWorld({3.0, 1.5});
    REQUIRE(w.x == Approx(3.0).margin(1e-9));
    REQUIRE(w.y == Approx(-1.5).margin(1e-9));

    Plan plan = makeEmptyPlan();
    plan.floors[0].ceilingHeight = 3.0;
    Floor second = plan.floors[0];
    second.id = "f2";
    second.ceilingHeight = 2.8;
    plan.floors.push_back(second);
    second.id = "f3";
    second.ceilingHeight = 3.2;
    plan.floors.push_back(second);
    const std::vector<double> levels = floorElevations(plan);
    REQUIRE(levels.size() == 3);
    REQUIRE(levels[0] == Approx(0.0).margin(1e-9));
    REQUIRE(levels[1] == Approx(3.0).margin(1e-9));
    REQUIRE(levels[2] == Approx(5.8).margin(1e-9));
}

TEST_CASE("wallSolids of the square is one solid with a hole", "[core][build]") {
    const Floor f = squareWithRoom();
    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 1);
    const WallSolid& s = solids[0];
    REQUIRE(s.height == Approx(3.0).margin(1e-9));
    REQUIRE(ringArea(s.outline.points) == Approx(17.64).margin(1e-6));
    REQUIRE(s.holes.size() == 1);
    REQUIRE(ringArea(s.holes[0].points) == Approx(-14.44).margin(1e-6));
    REQUIRE(-ringArea(s.holes[0].points) == Approx(roomShape(f, f.rooms[0]).area).margin(1e-6));

    REQUIRE(s.outline.edges.size() == s.outline.points.size());
    for (const WallEdge& e : s.outline.edges) {
        REQUIRE_FALSE(e.wall.empty());
        REQUIRE(e.side.has_value());
        REQUIRE(*e.side == WallSide::Left);
        REQUIRE(e.room.empty());
    }
    REQUIRE(wallsOf(s.outline) == std::set<Id>{"w1", "w2", "w3", "w4"});
    REQUIRE(s.holes[0].edges.size() == s.holes[0].points.size());
    for (const WallEdge& e : s.holes[0].edges) {
        REQUIRE_FALSE(e.wall.empty());
        REQUIRE(e.side.has_value());
        REQUIRE(*e.side == WallSide::Right);
        REQUIRE(e.room == "r1");
    }
    REQUIRE(wallsOf(s.holes[0]) == std::set<Id>{"w1", "w2", "w3", "w4"});

    REQUIRE(wallSolids(f) == solids);
}

TEST_CASE("wallSolids does not depend on the wall order", "[core][build]") {
    const Floor f = squareWithRoom();
    Floor shuffled = f;
    std::reverse(shuffled.walls.begin(), shuffled.walls.end());
    std::swap(shuffled.walls[0], shuffled.walls[2]);
    REQUIRE(wallSolids(shuffled) == wallSolids(f));
}

TEST_CASE("a half wall stops at the faces of the full walls", "[core][build]") {
    Floor f = squareWithRoom();
    f.points.push_back({"p5", 0.0, 2.0, 0.0});
    f.points.push_back({"p6", 4.0, 2.0, 0.0});
    Wall w;
    w.id = "w5";
    w.from = "p5";
    w.to = "p6";
    w.thickness = 0.2;
    w.height = 1.1;
    f.walls.push_back(w);

    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 2);
    REQUIRE(solids[0].height == Approx(3.0).margin(1e-9));
    REQUIRE(solids[1].height == Approx(1.1).margin(1e-9));
    REQUIRE(solids[1].holes.empty());
    REQUIRE(ringArea(solids[1].outline.points) == Approx(3.8 * 0.2).margin(1e-6));
    REQUIRE(wallsOf(solids[1].outline) == std::set<Id>{"w5"});
    REQUIRE(ringArea(solids[0].outline.points) == Approx(17.64).margin(1e-6));
}

TEST_CASE("a free standing wall has end faces with no side", "[core][build]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 3.0, 0.0, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    w.thickness = 0.2;
    f.walls.push_back(w);

    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 1);
    const WallRing& ring = solids[0].outline;
    REQUIRE(ring.edges.size() == 4);
    int ends = 0;
    int faces = 0;
    for (std::size_t i = 0; i < ring.edges.size(); ++i) {
        REQUIRE(ring.edges[i].wall == "w1");
        REQUIRE(ring.edges[i].room.empty());
        const double length = distance(ring.points[i], ring.points[(i + 1) % 4]);
        if (ring.edges[i].side) {
            ++faces;
            REQUIRE(length == Approx(3.2).margin(1e-9));
        } else {
            ++ends;
            REQUIRE(length == Approx(0.2).margin(1e-9));
        }
    }
    REQUIRE(ends == 2);
    REQUIRE(faces == 2);
}

TEST_CASE("a bent wall and a rounded corner give the union of the bands", "[core][build]") {
    Floor f = squareWithRoom();
    findWall(f, "w1")->bulge = -2.0;
    findPoint(f, "p3")->cornerRadius = 1.0;

    Clipper2Lib::PathsD bands;
    for (const Wall& w : f.walls) {
        for (const std::vector<Vec2>& band : wallBand(f, w)) {
            Clipper2Lib::PathD path;
            for (const Vec2& p : band) path.emplace_back(p.x, p.y);
            bands.push_back(path);
        }
    }
    const double expected = std::fabs(Clipper2Lib::Area(Clipper2Lib::Union(bands, Clipper2Lib::FillRule::NonZero, 8)));

    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 1);
    REQUIRE(solids[0].holes.size() == 1);
    const double solidArea = ringArea(solids[0].outline.points) + ringArea(solids[0].holes[0].points);
    REQUIRE(solidArea == Approx(expected).margin(1e-6));
    REQUIRE(-ringArea(solids[0].holes[0].points) == Approx(roomShape(f, f.rooms[0]).area).margin(1e-6));
    REQUIRE(solids[0].outline.points.size() > 8);
}

TEST_CASE("walls of different thickness meet in a clean corner", "[core][build]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 3.0, 0.0}};
    Wall a;
    a.id = "w1";
    a.from = "p1";
    a.to = "p2";
    a.thickness = 0.12;
    Wall b;
    b.id = "w2";
    b.from = "p2";
    b.to = "p3";
    b.thickness = 0.3;
    f.walls = {a, b};

    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 1);
    const std::vector<Vec2>& outline = solids[0].outline.points;
    const auto [minX, maxX] = range(outline, true);
    const auto [minY, maxY] = range(outline, false);
    REQUIRE(maxX == Approx(4.15).margin(1e-9));
    REQUIRE(maxY == Approx(0.06).margin(1e-9));
    REQUIRE(minX == Approx(-0.06).margin(1e-9));
    REQUIRE(minY == Approx(-3.15).margin(1e-9));
    REQUIRE(ringArea(outline) == Approx(4.21 * 0.12 + 0.3 * 3.09).margin(1e-6));
}

TEST_CASE("a half wall at a corner keeps square ends and the full wall is not mitred", "[core][build]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 3.0, 0.0}};
    Wall a;
    a.id = "w1";
    a.from = "p1";
    a.to = "p2";
    a.thickness = 0.2;
    Wall b = a;
    b.id = "w2";
    b.from = "p2";
    b.to = "p3";
    b.height = 1.1;
    f.walls = {a, b};

    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 2);
    const WallSolid& full = solids[0];
    const WallSolid& half = solids[1];
    REQUIRE(full.height == Approx(3.0).margin(1e-9));
    REQUIRE(half.height == Approx(1.1).margin(1e-9));
    REQUIRE(range(full.outline.points, true).second == Approx(4.1).margin(1e-9));
    REQUIRE(ringArea(full.outline.points) == Approx(4.2 * 0.2).margin(1e-6));
    REQUIRE(range(half.outline.points, true).second == Approx(4.1).margin(1e-9));
    REQUIRE(range(half.outline.points, false).second == Approx(-0.1).margin(1e-9));
    REQUIRE(ringArea(half.outline.points) == Approx(0.2 * 3.0).margin(1e-6));
}

TEST_CASE("a very short wall at a sharp angle keeps a square end instead of folding", "[core][build]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 0.3, 0.0, 0.0}, {"p3", -1.7, 3.4641016151377544, 0.0}};
    Wall a;
    a.id = "w1";
    a.from = "p1";
    a.to = "p2";
    a.thickness = 0.4;
    Wall b = a;
    b.id = "w2";
    b.from = "p2";
    b.to = "p3";
    f.walls = {a, b};

    const std::vector<std::vector<Vec2>> shortBand = detail::wallBandWith(f, f.walls[0], true);
    REQUIRE(shortBand.size() == 1);
    REQUIRE(ringArea(shortBand[0]) == Approx(0.7 * 0.4).margin(1e-9));
    for (const Vec2& p : shortBand[0]) {
        REQUIRE(p.x >= -0.2 - 1e-9);
        REQUIRE(p.x <= 0.5 + 1e-9);
    }

    const std::vector<std::vector<Vec2>> longBand = detail::wallBandWith(f, f.walls[1], true);
    REQUIRE(longBand.size() == 1);
    REQUIRE(ringArea(longBand[0]) == Approx(4.4 * 0.4).margin(1e-9));
    for (const Vec2& p : longBand[0]) {
        const Vec2 along{(p.x - 0.3) * -0.5 + p.y * 0.8660254037844386, 0.0};
        REQUIRE(along.x >= -0.2 - 1e-9);
    }

    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 1);
    REQUIRE(range(solids[0].outline.points, true).first == Approx(-1.8 - 0.2 * 0.8660254037844386).margin(1e-6));
    REQUIRE(solids[0].holes.empty());
}

TEST_CASE("a curved wall ends in a clean mitre against a straight wall", "[core][build]") {
    Floor f = squareWithRoom();
    findWall(f, "w1")->bulge = -1.0;

    Clipper2Lib::PathD centre;
    for (const char* id : {"w1", "w2", "w3", "w4"}) {
        const std::vector<Vec2> line = wallPath(f, *findWall(f, id)).centreLine;
        const bool join = !centre.empty();
        for (std::size_t i = join ? 1 : 0; i < line.size(); ++i) centre.emplace_back(line[i].x, line[i].y);
    }
    centre.pop_back();
    const Clipper2Lib::PathsD outer = Clipper2Lib::InflatePaths({centre}, 0.1, Clipper2Lib::JoinType::Miter,
                                                                Clipper2Lib::EndType::Polygon, 10.0, 8);
    const Clipper2Lib::PathsD inner = Clipper2Lib::InflatePaths({centre}, -0.1, Clipper2Lib::JoinType::Miter,
                                                                Clipper2Lib::EndType::Polygon, 10.0, 8);
    const double expected = std::fabs(Clipper2Lib::Area(outer)) - std::fabs(Clipper2Lib::Area(inner));

    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE(solids.size() == 1);
    REQUIRE(solids[0].holes.size() == 1);
    const double area = ringArea(solids[0].outline.points) + ringArea(solids[0].holes[0].points);
    REQUIRE(area == Approx(expected).margin(3e-3));
}

TEST_CASE("openingBuild of a door on a straight wall", "[core][build]") {
    const Floor f = squareWithRoom();
    const OpeningBuildResult r = openingBuild(f, door("w1", 2.0), 0.0);
    plynth::test::requireOk(r.status);
    const OpeningBuild& o = r.opening;
    REQUIRE(o.tangent.x == Approx(1.0).margin(1e-9));
    REQUIRE(o.tangent.y == Approx(0.0).margin(1e-9));
    REQUIRE(o.right.x == Approx(0.0).margin(1e-9));
    REQUIRE(o.right.y == Approx(-1.0).margin(1e-9));
    REQUIRE(o.centre.x == Approx(2.0).margin(1e-9));
    REQUIRE(o.centre.y == Approx(0.0).margin(1e-9));
    REQUIRE_FALSE(o.curved);
    REQUIRE(o.thickness == Approx(0.2).margin(1e-9));

    REQUIRE(o.cutter.right.size() == 4);
    REQUIRE(o.cutter.left.size() == 4);
    double lowest = 1e300;
    double highest = -1e300;
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(o.cutter.right[i].y == Approx(-0.15).margin(1e-9));
        REQUIRE(o.cutter.left[i].y == Approx(0.15).margin(1e-9));
        REQUIRE(o.cutter.right[i].x == Approx(o.cutter.left[i].x).margin(1e-9));
        REQUIRE(o.cutter.right[i].z == Approx(o.cutter.left[i].z).margin(1e-9));
        lowest = std::min(lowest, o.cutter.right[i].z);
        highest = std::max(highest, o.cutter.right[i].z);
    }
    REQUIRE(lowest == Approx(-0.05).margin(1e-9));
    REQUIRE(highest == Approx(2.1).margin(1e-9));

    REQUIRE(ringArea(o.threshold) == Approx(0.18).margin(1e-6));
    REQUIRE(o.innerOutline.size() == 4);
    const auto [uLo, uHi] = range(o.innerOutline, true);
    const auto [vLo, vHi] = range(o.innerOutline, false);
    REQUIRE(uLo == Approx(-0.4).margin(1e-9));
    REQUIRE(uHi == Approx(0.4).margin(1e-9));
    REQUIRE(vLo == Approx(0.0).margin(1e-9));
    REQUIRE(vHi == Approx(2.05).margin(1e-9));
    REQUIRE(o.outline.size() == 4);
}

TEST_CASE("openingBuild of a window keeps its box and has no threshold", "[core][build]") {
    const Floor f = squareWithRoom();
    Opening w;
    w.id = "o2";
    w.wall = "w1";
    w.offset = 2.0;
    w.type = OpeningType::Window;
    w.width = 1.2;
    w.height = 1.2;
    w.sill = 0.9;
    const OpeningBuildResult r = openingBuild(f, w, 0.0);
    plynth::test::requireOk(r.status);
    double lowest = 1e300;
    double highest = -1e300;
    for (const Vec3& p : r.opening.cutter.right) {
        lowest = std::min(lowest, p.z);
        highest = std::max(highest, p.z);
    }
    REQUIRE(lowest == Approx(0.9).margin(1e-9));
    REQUIRE(highest == Approx(2.1).margin(1e-9));
    const auto [uLo, uHi] = range(r.opening.innerOutline, true);
    const auto [vLo, vHi] = range(r.opening.innerOutline, false);
    REQUIRE(uLo == Approx(-0.55).margin(1e-9));
    REQUIRE(uHi == Approx(0.55).margin(1e-9));
    REQUIRE(vLo == Approx(0.05).margin(1e-9));
    REQUIRE(vHi == Approx(1.15).margin(1e-9));
    REQUIRE(r.opening.threshold.empty());
}

TEST_CASE("openingBuild of a round arch window has an inner outline inside the outline", "[core][build]") {
    const Floor f = squareWithRoom();
    Opening w;
    w.id = "o2";
    w.wall = "w1";
    w.offset = 2.0;
    w.type = OpeningType::Window;
    w.shape = "round_arch";
    w.width = 1.0;
    w.height = 1.4;
    w.sill = 0.9;
    const OpeningBuildResult r = openingBuild(f, w, 0.0);
    plynth::test::requireOk(r.status);
    const OpeningBuild& o = r.opening;
    REQUIRE(o.innerOutline.size() >= 3);
    REQUIRE(ringArea(o.innerOutline) > 0.0);
    REQUIRE(ringArea(o.innerOutline) < ringArea(o.outline));
    for (const Vec2& p : o.innerOutline) REQUIRE(insidePolygon(p, o.outline));
}

TEST_CASE("openingBuild on a curved wall follows the arc", "[core][build]") {
    Floor f = squareWithRoom();
    findWall(f, "w1")->bulge = -2.0;
    const WallCurve curve = wallCurve(f, *findWall(f, "w1"));
    REQUIRE(curve.curved);
    const OpeningBuildResult r = openingBuild(f, door("w1", curve.length / 2.0), 0.0);
    plynth::test::requireOk(r.status);
    const OpeningBuild& o = r.opening;
    REQUIRE(o.curved);
    REQUIRE(o.cutter.right.size() > 4);
    REQUIRE(o.cutter.right.size() == o.cutter.left.size());
    for (std::size_t i = 0; i < o.cutter.right.size(); ++i) {
        const Vec2 right = planToWorld({o.cutter.right[i].x, o.cutter.right[i].y});
        const Vec2 left = planToWorld({o.cutter.left[i].x, o.cutter.left[i].y});
        // A negative bulge bends to the left, so the right face is nearer the centre.
        REQUIRE(distance(right, curve.centre) == Approx(curve.radius - 0.15).margin(1e-9));
        REQUIRE(distance(left, curve.centre) == Approx(curve.radius + 0.15).margin(1e-9));
    }
}

TEST_CASE("an opening that reaches past its wall is not built", "[core][build]") {
    const Floor f = squareWithRoom();
    const Opening late = door("w1", 0.2);
    const Status expected = checkOpeningFit(f, late);
    REQUIRE_FALSE(expected.ok());
    const OpeningBuildResult r = openingBuild(f, late, 0.0);
    REQUIRE(r.status.code == ErrorCode::InvalidValue);
    REQUIRE(r.status.message == expected.message);
    REQUIRE(r.opening.cutter.right.empty());
}

TEST_CASE("openingBuild raises every height by the floor's level", "[core][build]") {
    const Floor f = squareWithRoom();
    const OpeningBuild low = openingBuild(f, door("w1", 2.0), 0.0).opening;
    const OpeningBuild high = openingBuild(f, door("w1", 2.0), 3.0).opening;
    REQUIRE(low.cutter.right.size() == high.cutter.right.size());
    for (std::size_t i = 0; i < low.cutter.right.size(); ++i) {
        REQUIRE(high.cutter.right[i].z == Approx(low.cutter.right[i].z + 3.0).margin(1e-9));
        REQUIRE(high.cutter.left[i].z == Approx(low.cutter.left[i].z + 3.0).margin(1e-9));
        REQUIRE(high.cutter.right[i].x == Approx(low.cutter.right[i].x).margin(1e-9));
    }
}

TEST_CASE("roomBuild gives the floor polygon, the levels and nothing for an open room", "[core][build]") {
    const Floor f = squareWithRoom();
    const std::optional<RoomBuild> room = roomBuild(f, f.rooms[0], 0.0);
    REQUIRE(room.has_value());
    REQUIRE(room->id == "r1");
    REQUIRE(room->area == Approx(14.44).margin(1e-6));
    REQUIRE(room->floorZ == Approx(0.0).margin(1e-9));
    REQUIRE(room->ceilingZ == Approx(3.0).margin(1e-9));
    REQUIRE(room->polygons.size() == 1);
    REQUIRE(room->polygons[0].holes.empty());
    REQUIRE(ringArea(room->polygons[0].outline) == Approx(14.44).margin(1e-6));

    const std::optional<RoomBuild> upstairs = roomBuild(f, f.rooms[0], 3.0);
    REQUIRE(upstairs->floorZ == Approx(3.0).margin(1e-9));
    REQUIRE(upstairs->ceilingZ == Approx(6.0).margin(1e-9));

    Floor open = f;
    open.walls.pop_back();
    open.rooms[0].walls = {"w1", "w2", "w3"};
    REQUIRE_FALSE(roomBuild(open, open.rooms[0], 0.0).has_value());
}

TEST_CASE("itemPlacement turns an item into Blender's axes", "[core][build]") {
    Item item;
    item.id = "i1";
    item.asset = "sofa";
    item.x = 3.2;
    item.y = 1.5;
    item.z = 0.2;
    item.rotation = 90.0;
    const ItemPlacement p = itemPlacement(item, 3.0);
    REQUIRE(p.location.x == Approx(3.2).margin(1e-9));
    REQUIRE(p.location.y == Approx(-1.5).margin(1e-9));
    REQUIRE(p.location.z == Approx(3.2).margin(1e-9));
    REQUIRE(p.rotationZ == Approx(-std::numbers::pi / 2.0).margin(1e-9));

    item.rotation = 0.0;
    REQUIRE(itemPlacement(item, 0.0).rotationZ == 0.0);
}

namespace {

// The square room of 4 m turned about the origin by an angle and moved, with one wall of half height.
Floor turnedHalfWallRoom(double degrees, bool half)
{
    Floor f = squareWithRoom();
    const double a = degrees * std::numbers::pi / 180.0;
    for (Point& pt : f.points) {
        const double x = pt.x;
        const double y = pt.y;
        pt.x = x * std::cos(a) - y * std::sin(a) + 3.0;
        pt.y = x * std::sin(a) + y * std::cos(a) + 3.0;
    }
    if (half) f.walls[0].height = 1.0;
    return f;
}

// Whether a ring goes along a line and straight back over it: a spike with no width.
bool hasSpike(const WallRing& ring)
{
    const std::size_t n = ring.points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = ring.points[i];
        const Vec2 b = ring.points[(i + 1) % n];
        const Vec2 c = ring.points[(i + 2) % n];
        const Vec2 u = detail::unit({b.x - a.x, b.y - a.y});
        const Vec2 v = detail::unit({c.x - b.x, c.y - b.y});
        if (std::fabs(u.x * v.y - u.y * v.x) < 1e-6 && detail::dot(u, v) < 0.0) return true;
    }
    return false;
}

}

TEST_CASE("wall solids have no zero width spikes, turned or not", "[core][build][geometry][spikes]") {
    for (const double angle : {0.0, 30.0, 45.0, 90.0}) {
        for (const bool half : {false, true}) {
            INFO("angle " << angle << " half " << half);
            const Floor f = turnedHalfWallRoom(angle, half);
            const std::vector<WallSolid> solids = wallSolids(f);
            REQUIRE_FALSE(solids.empty());
            double area = 0.0;
            for (const WallSolid& s : solids) {
                REQUIRE_FALSE(hasSpike(s.outline));
                for (const WallRing& h : s.holes) REQUIRE_FALSE(hasSpike(h));
                area += std::fabs(signedArea(s.outline.points));
                for (const WallRing& h : s.holes) area -= std::fabs(signedArea(h.points));
            }
            REQUIRE(area == Approx(3.2).margin(1e-9));
        }
    }
}
