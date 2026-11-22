#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/Snapping.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// A floor holding the square (0,0), (4,0), (4,4), (0,4) with 0.2 m walls w1 to w4 drawn in that order.
Floor squareFloor()
{
    Floor f;
    f.id = "f1";
    f.name = "Floor 1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 4.0, 0.0}, {"p4", 0.0, 4.0, 0.0}};
    const char* ends[4][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p1"}};
    for (int i = 0; i < 4; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.2;
        f.walls.push_back(w);
    }
    return f;
}

// The square as a project with its room r1 added at the middle.
Project squareProject()
{
    Project p = makeNewProject();
    p.plan.floors[0] = squareFloor();
    AddRoomCommand room("f1", {2.0, 2.0}, std::string("living"));
    plynth::test::requireOk(room.apply(p));
    return p;
}

// The area of the first room of the floor, as the floor inside its walls.
double floorArea(const Project& p)
{
    const Floor& f = p.plan.floors[0];
    return roomShape(f, f.rooms[0]).area;
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

// Applies, undoes and redoes a command, checking the project is equal after undo to before.
void roundTrip(Command& c, Project& p)
{
    const Project before = p;
    applyOk(c, p);
    const Project after = p;
    c.undo(p);
    REQUIRE(p == before);
    applyOk(c, p);
    REQUIRE(p == after);
}

}

TEST_CASE("curveBetween: a half circle", "[core][wallgeometry]") {
    const WallCurve c = curveBetween({0, 0}, {4, 0}, 2.0);
    REQUIRE(c.valid);
    REQUIRE(c.curved);
    REQUIRE(c.chord == Approx(4.0));
    REQUIRE(c.radius == Approx(2.0));
    REQUIRE(c.centre.x == Approx(2.0).margin(1e-9));
    REQUIRE(c.centre.y == Approx(0.0).margin(1e-9));
    REQUIRE(c.length == Approx(2.0 * kPi));
    REQUIRE(std::fabs(c.sweep) == Approx(kPi));
    const Vec2 top = pointAlong(c, kPi);
    REQUIRE(top.x == Approx(2.0).margin(1e-9));
    REQUIRE(top.y == Approx(2.0).margin(1e-9));
    const Vec2 t = tangentAlong(c, 0.0);
    REQUIRE(t.x == Approx(0.0).margin(1e-9));
    REQUIRE(t.y == Approx(1.0).margin(1e-9));
    REQUIRE(pointAlong(c, 0.0) == Vec2{0, 0});
    REQUIRE(pointAlong(c, 100.0) == Vec2{4, 0});
    REQUIRE(pointAlong(c, -1.0) == Vec2{0, 0});
}

TEST_CASE("curveBetween: a negative bulge and a tiny one", "[core][wallgeometry]") {
    const WallCurve c = curveBetween({0, 0}, {4, 0}, -1.0);
    REQUIRE(c.curved);
    REQUIRE(c.radius == Approx(2.5));
    REQUIRE(std::fabs(c.sweep) == Approx(4.0 * std::atan(0.5)));
    REQUIRE(c.length == Approx(4.6365).margin(1e-4));
    const Vec2 middle = pointAlong(c, c.length / 2.0);
    REQUIRE(middle.x == Approx(2.0).margin(1e-9));
    REQUIRE(middle.y == Approx(-1.0).margin(1e-9));

    const WallCurve tiny = curveBetween({0, 0}, {4, 0}, 0.0005);
    REQUIRE(tiny.valid);
    REQUIRE_FALSE(tiny.curved);
    REQUIRE(tiny.length == Approx(4.0));
    REQUIRE(tiny.radius == 0.0);

    REQUIRE_FALSE(curveBetween({1, 1}, {1, 1}, 0.0).valid);
    REQUIRE_FALSE(curveBetween({1, 1}, {1 + 1e-12, 1}, 0.5).valid);
    Floor f = squareFloor();
    Wall missing;
    missing.from = "p1";
    missing.to = "p99";
    REQUIRE_FALSE(wallCurve(f, missing).valid);
    REQUIRE(wallCurve(f, f.walls[0]).length == Approx(4.0));
}

TEST_CASE("the arc of a wall is on its circle and the tangent is a unit vector", "[core][wallgeometry]") {
    for (double bulge : {0.3, -0.8, 1.7, -2.0}) {
        const WallCurve c = curveBetween({1.0, 2.0}, {4.0, -1.0}, bulge * 0.5);
        INFO(bulge);
        for (int k = 0; k <= 10; ++k) {
            const double s = c.length * k / 10.0;
            const Vec2 p = pointAlong(c, s);
            REQUIRE(distance(p, c.centre) == Approx(c.radius).margin(1e-9));
            const Vec2 t = tangentAlong(c, s);
            REQUIRE(std::hypot(t.x, t.y) == Approx(1.0));
        }
        REQUIRE(distance(pointAlong(c, c.length), c.to) < 1e-9);
    }
}

TEST_CASE("curvePoints cuts by arcSegmentCount with exact ends", "[core][wallgeometry]") {
    const WallCurve c = curveBetween({0, 0}, {4, 0}, 2.0);
    const std::vector<Vec2> all = curvePoints(c, 0.0, c.length);
    REQUIRE(all.size() == static_cast<std::size_t>(arcSegmentCount(2.0, kPi)) + 1);
    REQUIRE(all.front() == Vec2{0, 0});
    REQUIRE(all.back() == Vec2{4, 0});

    const std::vector<Vec2> part = curvePoints(c, kPi / 2.0, kPi);
    REQUIRE(part.size() == static_cast<std::size_t>(arcSegmentCount(2.0, kPi / 4.0)) + 1);
    REQUIRE(part.front() == pointAlong(c, kPi / 2.0));
    REQUIRE(part.back() == pointAlong(c, kPi));

    const WallCurve straight = curveBetween({0, 0}, {4, 0}, 0.0);
    REQUIRE(curvePoints(straight, 1.0, 3.0) == std::vector<Vec2>{{1, 0}, {3, 0}});
}

TEST_CASE("projectOntoCurve", "[core][wallgeometry]") {
    const WallCurve c = curveBetween({0, 0}, {4, 0}, 2.0);
    SECTION("outside the arc is the right side, inside is the left") {
        const CurveProjection out = projectOntoCurve({2.0, 2.05}, c);
        REQUIRE(out.offset == Approx(kPi));
        REQUIRE(out.distance == Approx(0.05));
        REQUIRE(out.side == WallSide::Right);
        const CurveProjection in = projectOntoCurve({2.0, 1.9}, c);
        REQUIRE(in.offset == Approx(kPi));
        REQUIRE(in.distance == Approx(0.1).margin(1e-9));
        REQUIRE(in.side == WallSide::Left);
    }
    SECTION("past the ends the closest end wins") {
        const CurveProjection p = projectOntoCurve({-1.0, -1.0}, c);
        REQUIRE(p.offset == 0.0);
        REQUIRE(p.position == Vec2{0, 0});
        REQUIRE(p.distance == Approx(std::sqrt(2.0)));
        const CurveProjection q = projectOntoCurve({5.0, -0.5}, c);
        REQUIRE(q.offset == Approx(c.length));
    }
    SECTION("a straight wall") {
        const WallCurve s = curveBetween({0, 0}, {4, 0}, 0.0);
        const CurveProjection below = projectOntoCurve({1.0, 0.5}, s);
        REQUIRE(below.offset == Approx(1.0));
        REQUIRE(below.side == WallSide::Right);
        REQUIRE(projectOntoCurve({1.0, -0.5}, s).side == WallSide::Left);
    }
    SECTION("an invalid curve") {
        const CurveProjection p = projectOntoCurve({3.0, 4.0}, WallCurve{});
        REQUIRE(p.offset == 0.0);
        REQUIRE(p.distance == Approx(5.0));
    }
}

TEST_CASE("bulgeFromPosition", "[core][wallgeometry]") {
    REQUIRE(bulgeFromPosition({0, 0}, {4, 0}, {2, 3}) == Approx(2.0));
    REQUIRE(bulgeFromPosition({0, 0}, {4, 0}, {2, -1}) == Approx(-1.0));
    REQUIRE(bulgeFromPosition({0, 0}, {4, 0}, {2, 1.2}) == Approx(1.2));
    REQUIRE(bulgeFromPosition({0, 0}, {4, 0}, {2, -9}) == Approx(-2.0));
    REQUIRE(bulgeFromPosition({1, 1}, {1, 1}, {5, 5}) == 0.0);
    // Dragging to the right of a wall that runs south is toward the west.
    REQUIRE(bulgeFromPosition({0, 0}, {0, 4}, {-1.0, 2.0}) == Approx(1.0));
}

TEST_CASE("wallSideOf picks the face the wall view shows", "[core][wallgeometry]") {
    const Floor f = squareFloor();
    // w2 runs south from (4,0) to (4,4): its right side is west, inside the square.
    REQUIRE(wallSideOf(f, f.walls[1], {3.0, 2.0}) == WallSide::Right);
    REQUIRE(wallSideOf(f, f.walls[1], {5.0, 2.0}) == WallSide::Left);
    // w1 runs east: right is south, inside.
    REQUIRE(wallSideOf(f, f.walls[0], {2.0, 1.0}) == WallSide::Right);
}

TEST_CASE("wallHeight", "[core][wallgeometry]") {
    Floor f = squareFloor();
    f.ceilingHeight = 2.7;
    REQUIRE(wallHeight(f, f.walls[0]) == 2.7);
    f.walls[0].height = 1.1;
    REQUIRE(wallHeight(f, f.walls[0]) == 1.1);
}

TEST_CASE("cornerRounding", "[core][wallgeometry]") {
    SECTION("radius 1 at the corner of the square") {
        Floor f = squareFloor();
        findPoint(f, "p3")->cornerRadius = 1.0;
        const CornerRounding r = cornerRounding(f, *findPoint(f, "p3"));
        REQUIRE(r.rounded);
        REQUIRE(r.radius == Approx(1.0));
        REQUIRE(r.trim == Approx(1.0));
        REQUIRE(r.centre.x == Approx(3.0));
        REQUIRE(r.centre.y == Approx(3.0));
        REQUIRE(r.firstWall == "w2");
        REQUIRE(r.secondWall == "w3");
        REQUIRE(r.onFirst.x == Approx(4.0));
        REQUIRE(r.onFirst.y == Approx(3.0));
        REQUIRE(r.onSecond.x == Approx(3.0));
        REQUIRE(r.onSecond.y == Approx(4.0));
        REQUIRE(r.arc.size() == static_cast<std::size_t>(arcSegmentCount(1.0, kPi / 2.0)) + 1);
        REQUIRE(r.arc.front() == r.onFirst);
        REQUIRE(r.arc.back() == r.onSecond);
        for (const Vec2& p : r.arc) REQUIRE(distance(p, r.centre) == Approx(1.0).margin(1e-9));
    }
    SECTION("the radius is limited so the trim is at most half of each wall") {
        Floor f;
        f.points = {{"a", 0, 0, 2.0}, {"b", 1, 0, 0.0}, {"c", 0, 4, 0.0}};
        Wall w1;
        w1.id = "w1";
        w1.from = "a";
        w1.to = "b";
        Wall w2;
        w2.id = "w2";
        w2.from = "a";
        w2.to = "c";
        f.walls = {w1, w2};
        const CornerRounding r = cornerRounding(f, f.points[0]);
        REQUIRE(r.rounded);
        REQUIRE(r.radius == Approx(0.5));
        REQUIRE(r.trim == Approx(0.5));
    }
    SECTION("a point with three walls, a curved wall, one wall or collinear walls is not rounded") {
        Floor f = squareFloor();
        findPoint(f, "p3")->cornerRadius = 1.0;
        Wall extra;
        extra.id = "w9";
        extra.from = "p3";
        extra.to = "p1";
        f.walls.push_back(extra);
        CornerRounding r = cornerRounding(f, *findPoint(f, "p3"));
        REQUIRE_FALSE(r.rounded);
        REQUIRE(r.radius == 0.0);

        f = squareFloor();
        findPoint(f, "p3")->cornerRadius = 1.0;
        findWall(f, "w2")->bulge = 0.5;
        REQUIRE_FALSE(cornerRounding(f, *findPoint(f, "p3")).rounded);

        f = squareFloor();
        f.walls.pop_back();
        f.walls.pop_back();
        findPoint(f, "p3")->cornerRadius = 1.0;
        REQUIRE_FALSE(cornerRounding(f, *findPoint(f, "p3")).rounded);

        Floor line;
        line.points = {{"a", 0, 0, 0.0}, {"b", 2, 0, 1.0}, {"c", 4, 0, 0.0}};
        Wall w1;
        w1.id = "w1";
        w1.from = "a";
        w1.to = "b";
        Wall w2;
        w2.id = "w2";
        w2.from = "b";
        w2.to = "c";
        line.walls = {w1, w2};
        REQUIRE_FALSE(cornerRounding(line, line.points[1]).rounded);
    }
    SECTION("zero radius is not rounded") {
        const Floor f = squareFloor();
        REQUIRE_FALSE(cornerRounding(f, *findPoint(f, "p3")).rounded);
    }
}

TEST_CASE("wallPath trims a rounded corner and adds half the arc", "[core][wallgeometry]") {
    Floor f = squareFloor();
    findPoint(f, "p3")->cornerRadius = 1.0;
    const WallPath w2 = wallPath(f, *findWall(f, "w2"));
    REQUIRE_FALSE(w2.roundedStart);
    REQUIRE(w2.roundedEnd);
    REQUIRE(w2.endTrim == Approx(1.0));
    REQUIRE(w2.startTrim == 0.0);
    REQUIRE(w2.centreLine.front() == Vec2{4, 0});
    // Ends at the middle of the arc, which is on the line from the corner to the arc's centre.
    const Vec2 end = w2.centreLine.back();
    const double r = std::sqrt(0.5);
    REQUIRE(end.x == Approx(3.0 + r).margin(1e-9));
    REQUIRE(end.y == Approx(3.0 + r).margin(1e-9));

    const WallPath w3 = wallPath(f, *findWall(f, "w3"));
    REQUIRE(w3.roundedStart);
    REQUIRE(w3.centreLine.front().x == Approx(end.x).margin(1e-12));
    REQUIRE(w3.centreLine.front().y == Approx(end.y).margin(1e-12));
    REQUIRE(w3.centreLine.back() == Vec2{0, 4});

    for (const WallPath* path : {&w2, &w3}) {
        for (std::size_t i = 1; i < path->centreLine.size(); ++i) {
            REQUIRE(distance(path->centreLine[i - 1], path->centreLine[i]) > 1e-9);
        }
    }
    const WallPath plain = wallPath(f, *findWall(f, "w1"));
    REQUIRE(plain.centreLine == std::vector<Vec2>{{0, 0}, {4, 0}});
}

TEST_CASE("wallBand of a straight wall is the rectangle with extended ends", "[core][wallgeometry]") {
    const Floor f = squareFloor();
    const auto band = wallBand(f, f.walls[0]);
    REQUIRE(band.size() == 1);
    REQUIRE(band[0].size() == 4);
    REQUIRE(signedArea(band[0]) == Approx(4.2 * 0.2));
    double minX = 99, maxX = -99, minY = 99, maxY = -99;
    for (const Vec2& p : band[0]) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    REQUIRE(minX == Approx(-0.1));
    REQUIRE(maxX == Approx(4.1));
    REQUIRE(minY == Approx(-0.1));
    REQUIRE(maxY == Approx(0.1));

    Wall missing;
    missing.from = "p1";
    missing.to = "p99";
    REQUIRE(wallBand(f, missing).empty());
}

TEST_CASE("wallBand of a curved wall follows the arc", "[core][wallgeometry]") {
    Floor f = squareFloor();
    findWall(f, "w1")->bulge = -2.0;
    const auto band = wallBand(f, f.walls[0]);
    REQUIRE(band.size() == 1);
    REQUIRE(signedArea(band[0]) > 0.0);
    // The band of a half circle of radius 2, 0.2 thick, with ends extended by 0.1: close to a half ring.
    const double ring = kPi * (2.1 * 2.1 - 1.9 * 1.9) / 2.0;
    REQUIRE(signedArea(band[0]) == Approx(ring + 2 * 0.2 * 0.1).epsilon(0.005));
    for (const Vec2& p : band[0]) {
        const double d = distance(p, {2.0, 0.0});
        REQUIRE(d < 2.1 + 0.2);
        REQUIRE(d > 1.9 - 0.2);
    }
}

TEST_CASE("the bands of two walls at a rounded corner meet without a notch", "[core][wallgeometry]") {
    Floor f = squareFloor();
    findPoint(f, "p3")->cornerRadius = 1.0;
    const auto b2 = wallBand(f, *findWall(f, "w2"));
    const auto b3 = wallBand(f, *findWall(f, "w3"));
    REQUIRE(b2.size() == 1);
    REQUIRE(b3.size() == 1);

    // Nothing is inside both bands.
    int both = 0;
    for (double x = 2.6; x <= 4.2; x += 0.01) {
        for (double y = 2.6; y <= 4.2; y += 0.01) {
            if (insidePolygon({x, y}, b2[0]) && insidePolygon({x, y}, b3[0])) ++both;
        }
    }
    REQUIRE(both == 0);

    // Every place across the band along the corner arc is in one of them.
    const CornerRounding r = cornerRounding(f, *findPoint(f, "p3"));
    const double sweep = kPi / 2.0;
    const double a0 = std::atan2(r.onFirst.y - r.centre.y, r.onFirst.x - r.centre.x);
    for (int k = 0; k < 90; ++k) {
        const double a = a0 + sweep * (k + 0.5) / 90.0;
        for (double across : {0.92, 1.0, 1.08}) {
            const Vec2 p{r.centre.x + across * std::cos(a), r.centre.y + across * std::sin(a)};
            REQUIRE((insidePolygon(p, b2[0]) || insidePolygon(p, b3[0])));
        }
    }
}

TEST_CASE("the square room: areas with a curved wall and a rounded corner", "[core][wallgeometry][rooms]") {
    Project p = squareProject();
    Floor& f = p.plan.floors[0];
    const auto areaOfRing = [&]() { return findClosedAreas(f)[0].centreLineArea; };

    SECTION("the plain square") {
        REQUIRE(floorArea(p) == Approx(14.44).margin(1e-6));
        REQUIRE(areaOfRing() == Approx(16.0));
    }
    SECTION("a half circle wall away from the room") {
        SetWallBulgeCommand bend("w1", -2.0);
        applyOk(bend, p);
        REQUIRE(areaOfRing() == Approx(16.0 + 2.0 * kPi).epsilon(0.005));
        REQUIRE(floorArea(p) == Approx(20.49).epsilon(0.005));
        const RoomShape shape = roomShape(f, f.rooms[0]);
        REQUIRE(shape.closed);
        std::vector<Vec2> ring0 = shape.rings[0];
        REQUIRE(insidePolygon(shape.labelPoint, ring0));
        // The label point is on the floor, not in a wall band.
        for (const Wall& w : f.walls) {
            for (const auto& band : wallBand(f, w)) REQUIRE_FALSE(insidePolygon(shape.labelPoint, band));
        }
    }
    SECTION("a rounded corner") {
        SetCornerRadiusCommand round("p3", 1.0);
        applyOk(round, p);
        REQUIRE(areaOfRing() == Approx(16.0 - (1.0 - kPi / 4.0)).epsilon(0.002));
        REQUIRE(floorArea(p) == Approx(14.44 - 0.81 * (1.0 - kPi / 4.0)).epsilon(0.005));
        REQUIRE(floorArea(p) == Approx(14.27).epsilon(0.005));
    }
    SECTION("both together") {
        SetWallBulgeCommand bend("w1", -2.0);
        applyOk(bend, p);
        SetCornerRadiusCommand round("p3", 1.0);
        applyOk(round, p);
        REQUIRE(floorArea(p) == Approx(20.32).epsilon(0.005));
    }
}

TEST_CASE("a room closed by a straight wall and a half circle wall", "[core][wallgeometry][rooms]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0, 0, 0.0}, {"p2", 4, 0, 0.0}};
    Wall chord;
    chord.id = "w1";
    chord.from = "p1";
    chord.to = "p2";
    Wall arc;
    arc.id = "w2";
    arc.from = "p2";
    arc.to = "p1";
    arc.bulge = 2.0;
    f.walls = {chord, arc};

    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].walls.size() == 2);
    REQUIRE(areas[0].centreLineArea == Approx(kPi * 4.0 / 2.0).epsilon(0.005));
    REQUIRE(insidePolygon(areas[0].inside, areas[0].rings[0]));

    // The same two points joined by the same bend, written the other way round, is one wall.
    Floor again = f;
    Wall twin;
    twin.id = "w3";
    twin.from = "p1";
    twin.to = "p2";
    twin.bulge = -2.0;
    again.walls.push_back(twin);
    REQUIRE(findClosedAreas(again).size() == 1);
}

TEST_CASE("a curved wall bending into a room keeps the rooms on both sides", "[core][wallgeometry][rooms]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0, 0, 0.0}, {"p2", 4, 0, 0.0}, {"p3", 8, 0, 0.0}, {"p4", 8, 4, 0.0},
                {"p5", 4, 4, 0.0}, {"p6", 0, 4, 0.0}};
    const char* ends[7][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p5"}, {"p5", "p6"}, {"p6", "p1"}, {"p2", "p5"}};
    for (int i = 0; i < 7; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        f.walls.push_back(w);
    }
    // w7 runs south; positive bulge bends to its right, which is west, into the left room.
    f.walls[6].bulge = 1.0;

    const double segment = 2.5 * 2.5 / 2.0 * (2.0 * std::asin(0.8) - std::sin(2.0 * std::asin(0.8)));
    const auto check = [&](const Floor& floor) {
        const auto areas = findClosedAreas(floor);
        REQUIRE(areas.size() == 2);
        std::vector<double> sizes{areas[0].centreLineArea, areas[1].centreLineArea};
        std::sort(sizes.begin(), sizes.end());
        REQUIRE(sizes[0] == Approx(16.0 - segment).epsilon(0.005));
        REQUIRE(sizes[1] == Approx(16.0 + segment).epsilon(0.005));
        std::set<std::string> first(areas[0].walls.begin(), areas[0].walls.end());
        std::set<std::string> second(areas[1].walls.begin(), areas[1].walls.end());
        REQUIRE(first.count("w7") == 1);
        REQUIRE(second.count("w7") == 1);
    };
    check(f);

    SECTION("shuffled wall and point order gives the same result") {
        std::mt19937 rng(7);
        for (int round = 0; round < 5; ++round) {
            Floor shuffled = f;
            std::shuffle(shuffled.walls.begin(), shuffled.walls.end(), rng);
            std::shuffle(shuffled.points.begin(), shuffled.points.end(), rng);
            check(shuffled);
            std::vector<std::vector<std::string>> a;
            std::vector<std::vector<std::string>> b;
            for (const auto& area : findClosedAreas(f)) a.push_back(area.walls);
            for (const auto& area : findClosedAreas(shuffled)) b.push_back(area.walls);
            REQUIRE(a == b);
        }
    }
}

TEST_CASE("an arc tangent to a straight wall at a point", "[core][wallgeometry][rooms]") {
    // The arc from (0, 0) to (4, 0) with bulge 1 starts heading along (0.6, 0.8), the same way as the straight
    // wall from (0, 0) to (3, 4). A third wall closes the triangle.
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0, 0, 0.0}, {"p2", 4, 0, 0.0}, {"p3", 3, 4, 0.0}};
    Wall arc;
    arc.id = "w1";
    arc.from = "p1";
    arc.to = "p2";
    arc.bulge = 1.0;
    Wall line;
    line.id = "w2";
    line.from = "p1";
    line.to = "p3";
    Wall close;
    close.id = "w3";
    close.from = "p2";
    close.to = "p3";
    f.walls = {arc, line, close};
    const double segment = 2.5 * 2.5 / 2.0 * (2.0 * std::asin(0.8) - std::sin(2.0 * std::asin(0.8)));
    for (int order = 0; order < 2; ++order) {
        if (order == 1) std::reverse(f.walls.begin(), f.walls.end());
        const auto areas = findClosedAreas(f);
        REQUIRE(areas.size() == 1);
        REQUIRE(areas[0].walls.size() == 3);
        REQUIRE(areas[0].centreLineArea == Approx(8.0 - segment).epsilon(0.005));
    }
}

TEST_CASE("two arcs between the same points make a lens", "[core][wallgeometry][rooms]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0, 0, 0.0}, {"p2", 4, 0, 0.0}};
    Wall a;
    a.id = "w1";
    a.from = "p1";
    a.to = "p2";
    a.bulge = 0.5;
    Wall b;
    b.id = "w2";
    b.from = "p1";
    b.to = "p2";
    b.bulge = 1.0;
    f.walls = {a, b};
    const auto areas = findClosedAreas(f);
    REQUIRE(areas.size() == 1);
    // The lens between sagitta 0.5 and sagitta 1.0 over a chord of 4.
    const auto segment = [](double sagitta) {
        const double radius = (4.0 + sagitta * sagitta) / (2.0 * sagitta);
        const double angle = 2.0 * std::asin(2.0 / radius);
        return radius * radius / 2.0 * (angle - std::sin(angle));
    };
    REQUIRE(areas[0].centreLineArea == Approx(segment(1.0) - segment(0.5)).epsilon(0.01));
}

TEST_CASE("SplitWallCommand splits a curved wall on its own circle", "[core][wallgeometry][command]") {
    Project p = squareProject();
    Floor& f = p.plan.floors[0];
    f.walls[0].bulge = 2.0;
    Opening o;
    o.id = "o1";
    o.wall = "w1";
    o.offset = 4.5;
    o.width = 0.9;
    o.height = 2.1;
    f.openings.push_back(o);
    plynth::test::requirePlanValid(p.plan);

    SplitWallCommand c("w1", kPi);
    roundTrip(c, p);
    const Floor& after = p.plan.floors[0];
    const Point* mid = findPoint(after, c.newPointId());
    REQUIRE(mid != nullptr);
    REQUIRE(mid->x == Approx(2.0).margin(1e-9));
    REQUIRE(mid->y == Approx(2.0).margin(1e-9));
    const double part = 2.0 * (1.0 - std::cos(kPi / 4.0));
    const Wall* first = findWall(after, "w1");
    const Wall* second = findWall(after, c.newWallId());
    REQUIRE(first->bulge == Approx(part));
    REQUIRE(second->bulge == Approx(part));
    REQUIRE(wallLength(after, *first) == Approx(kPi));
    REQUIRE(wallLength(after, *second) == Approx(kPi));
    // Both parts are on the same circle.
    for (const Wall* w : {first, second}) {
        const WallCurve curve = wallCurve(after, *w);
        REQUIRE(curve.radius == Approx(2.0));
        REQUIRE(curve.centre.x == Approx(2.0).margin(1e-9));
        REQUIRE(curve.centre.y == Approx(0.0).margin(1e-9));
    }
    const Opening* moved = findOpening(after, "o1");
    REQUIRE(moved->wall == c.newWallId());
    REQUIRE(moved->offset == Approx(4.5 - kPi));
    REQUIRE(c.newPointId() == "p5");
}

TEST_CASE("SplitWallCommand keeps a negative bulge's sign and refuses a place covered by an opening", "[core][wallgeometry][command]") {
    Project p = squareProject();
    Floor& f = p.plan.floors[0];
    f.walls[0].bulge = -1.0;
    SplitWallCommand c("w1", 1.0);
    applyOk(c, p);
    REQUIRE(findWall(p.plan.floors[0], "w1")->bulge < 0.0);
    REQUIRE(findWall(p.plan.floors[0], c.newWallId())->bulge < 0.0);

    Project q = squareProject();
    q.plan.floors[0].walls[0].bulge = 2.0;
    Opening o;
    o.id = "o1";
    o.wall = "w1";
    o.offset = 3.0;
    q.plan.floors[0].openings.push_back(o);
    SplitWallCommand covered("w1", 3.0);
    applyRefused(covered, q, ErrorCode::InUse);
    SplitWallCommand past("w1", 2.0 * kPi);
    applyRefused(past, q, ErrorCode::InvalidValue);
}

TEST_CASE("DrawWallCommand from a point snapped onto a curved wall splits it", "[core][wallgeometry][command]") {
    Project p = squareProject();
    Floor& f = p.plan.floors[0];
    f.walls[0].bulge = 2.0;

    SnapOptions options;
    options.radius = 0.2;
    const SnapResult snap = snapPosition(f, {2.0, 2.04}, options);
    REQUIRE(snap.kind == SnapKind::Wall);
    REQUIRE(snap.wallId == "w1");
    REQUIRE(snap.wallOffset == Approx(kPi).epsilon(0.001));
    REQUIRE(snap.position.x == Approx(2.0).margin(1e-6));
    REQUIRE(snap.position.y == Approx(2.0).margin(1e-6));

    DrawWallCommand draw("f1", wallEndFromSnap(snap), WallEnd::newPoint({2.0, 3.0}));
    roundTrip(draw, p);
    REQUIRE(draw.splitWallIds() == std::vector<Id>{"w1"});
    REQUIRE(draw.newPointIds().size() == 2);
    const Floor& after = p.plan.floors[0];
    const Point* split = findPoint(after, draw.fromPointId());
    REQUIRE(split->x == Approx(2.0).margin(1e-6));
    REQUIRE(split->y == Approx(2.0).margin(1e-6));
    REQUIRE(findWall(after, "w1")->bulge != 0.0);
}

TEST_CASE("snapPosition onto a curved wall", "[core][wallgeometry][snap]") {
    Floor f = squareFloor();
    f.walls[0].bulge = 1.0;
    SnapOptions options;
    options.radius = 0.2;
    const SnapResult onArc = snapPosition(f, {2.0, 1.05}, options);
    REQUIRE(onArc.kind == SnapKind::Wall);
    REQUIRE(onArc.wallId == "w1");
    REQUIRE(onArc.position.y == Approx(1.0).margin(1e-9));
    const WallCurve curve = wallCurve(f, f.walls[0]);
    REQUIRE(onArc.wallOffset == Approx(curve.length / 2.0).epsilon(0.001));

    // The straight chord is no longer a wall there.
    options.radius = 0.1;
    REQUIRE(snapPosition(f, {2.0, 0.05}, options).kind == SnapKind::None);

    // With a direction lock the place is where the vertical line meets the arc.
    options.radius = 0.5;
    options.orthogonalFrom = Vec2{1.0, 3.0};
    const SnapResult locked = snapPosition(f, {1.0, 0.7}, options);
    REQUIRE(locked.kind == SnapKind::Wall);
    REQUIRE(locked.position.x == Approx(1.0));
    REQUIRE(distance(locked.position, curve.centre) == Approx(curve.radius).margin(1e-9));
}

TEST_CASE("pickAt uses the wall band, including the outer side of a curve", "[core][wallgeometry][snap]") {
    Floor f = squareFloor();
    f.walls[0].bulge = 2.0;
    // The top of the half circle is at (2, 2). Its outer side is at y 2.1.
    REQUIRE(pickAt(f, {2.0, 2.08}, 0.01) == Pick{PickKind::Wall, "w1"});
    REQUIRE(pickAt(f, {2.0, 1.92}, 0.01) == Pick{PickKind::Wall, "w1"});
    REQUIRE(pickAt(f, {2.0, 2.2}, 0.05).kind == PickKind::None);
    // The old straight line is no longer a wall.
    REQUIRE(pickAt(f, {2.0, 0.0}, 0.01).kind == PickKind::None);
    // With a radius, a place near the band counts.
    REQUIRE(pickAt(f, {2.0, 2.18}, 0.1) == Pick{PickKind::Wall, "w1"});
}

TEST_CASE("SetWallBulgeCommand", "[core][wallgeometry][command]") {
    Project p = squareProject();
    SECTION("sets, undoes and redoes") {
        SetWallBulgeCommand c("w1", 0.75);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].walls[0].bulge == 0.75);
        REQUIRE(c.changes() == Change::Walls);
        REQUIRE(c.floorId() == "f1");
        REQUIRE(c.name() == "Bend wall");
    }
    SECTION("a bulge below 0.001 is stored as 0") {
        SetWallBulgeCommand bend("w1", 0.5);
        applyOk(bend, p);
        SetWallBulgeCommand c("w1", 0.0005);
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].walls[0].bulge == 0.0);
        SetWallBulgeCommand negative("w1", -0.0009);
        applyOk(negative, p);
        REQUIRE(p.plan.floors[0].walls[0].bulge == 0.0);
    }
    SECTION("the largest bulge is a half circle") {
        SetWallBulgeCommand ok("w1", 2.0);
        applyOk(ok, p);
        SetWallBulgeCommand ok2("w1", -2.0);
        applyOk(ok2, p);
        SetWallBulgeCommand tooBig("w1", 2.01);
        applyRefused(tooBig, p, ErrorCode::InvalidValue);
        SetWallBulgeCommand tooBigNegative("w1", -2.01);
        applyRefused(tooBigNegative, p, ErrorCode::InvalidValue);
    }
    SECTION("refuses a radius that is not above half the thickness") {
        p.plan.floors[0].walls[0].thickness = 6.0;
        SetWallBulgeCommand c("w1", 2.0);
        applyRefused(c, p, ErrorCode::InvalidValue);
        p.plan.floors[0].walls[0].thickness = 3.9;
        SetWallBulgeCommand ok("w1", 2.0);
        applyOk(ok, p);
    }
    SECTION("refuses unknown walls and bad numbers") {
        SetWallBulgeCommand unknown("w9", 0.5);
        applyRefused(unknown, p, ErrorCode::NotFound);
        SetWallBulgeCommand nan("w1", kNaN);
        applyRefused(nan, p, ErrorCode::InvalidValue);
        SetWallBulgeCommand inf("w1", std::numeric_limits<double>::infinity());
        applyRefused(inf, p, ErrorCode::InvalidValue);
    }
    SECTION("merges only with the same non zero key and wall") {
        SetWallBulgeCommand first("w1", 0.4, 7);
        applyOk(first, p);
        SetWallBulgeCommand sameKey("w1", 0.9, 7);
        applyOk(sameKey, p);
        REQUIRE(first.mergeWith(sameKey));
        first.undo(p);
        REQUIRE(p.plan.floors[0].walls[0].bulge == 0.0);
        applyOk(first, p);
        REQUIRE(p.plan.floors[0].walls[0].bulge == 0.9);

        SetWallBulgeCommand otherKey("w1", 1.0, 8);
        SetWallBulgeCommand otherWall("w2", 1.0, 7);
        SetWallBulgeCommand noKeyA("w1", 1.0);
        SetWallBulgeCommand noKeyB("w1", 1.5);
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(first.mergeWith(otherWall));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
    }
}

TEST_CASE("SetCornerRadiusCommand", "[core][wallgeometry][command]") {
    Project p = squareProject();
    SECTION("sets, undoes and redoes") {
        SetCornerRadiusCommand c("p3", 0.6);
        roundTrip(c, p);
        REQUIRE(findPoint(p.plan.floors[0], "p3")->cornerRadius == 0.6);
        REQUIRE(c.changes() == Change::Points);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("refuses a negative radius, NaN and unknown points") {
        SetCornerRadiusCommand negative("p3", -0.1);
        applyRefused(negative, p, ErrorCode::InvalidValue);
        SetCornerRadiusCommand nan("p3", kNaN);
        applyRefused(nan, p, ErrorCode::InvalidValue);
        SetCornerRadiusCommand inf("p3", std::numeric_limits<double>::infinity());
        applyRefused(inf, p, ErrorCode::InvalidValue);
        SetCornerRadiusCommand unknown("p9", 1.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
    }
    SECTION("stores 5 as given even though less is used") {
        SetCornerRadiusCommand c("p3", 5.0);
        applyOk(c, p);
        REQUIRE(findPoint(p.plan.floors[0], "p3")->cornerRadius == 5.0);
        const CornerRounding r = cornerRounding(p.plan.floors[0], *findPoint(p.plan.floors[0], "p3"));
        REQUIRE(r.rounded);
        REQUIRE(r.radius == Approx(2.0));
        SetCornerRadiusCommand zero("p3", 0.0);
        applyOk(zero, p);
        REQUIRE_FALSE(cornerRounding(p.plan.floors[0], *findPoint(p.plan.floors[0], "p3")).rounded);
    }
    SECTION("merges only with the same non zero key and point") {
        SetCornerRadiusCommand first("p3", 0.3, 4);
        applyOk(first, p);
        SetCornerRadiusCommand sameKey("p3", 0.8, 4);
        applyOk(sameKey, p);
        REQUIRE(first.mergeWith(sameKey));
        first.undo(p);
        REQUIRE(findPoint(p.plan.floors[0], "p3")->cornerRadius == 0.0);
        applyOk(first, p);
        REQUIRE(findPoint(p.plan.floors[0], "p3")->cornerRadius == 0.8);
        SetCornerRadiusCommand otherPoint("p2", 1.0, 4);
        SetCornerRadiusCommand otherKey("p3", 1.0, 5);
        SetCornerRadiusCommand noKeyA("p3", 1.0);
        SetCornerRadiusCommand noKeyB("p3", 1.2);
        REQUIRE_FALSE(first.mergeWith(otherPoint));
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
    }
}

TEST_CASE("the plan stays valid after every command on curved walls", "[core][wallgeometry][command]") {
    Project p = squareProject();
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<SetWallBulgeCommand>("w1", -2.0));
    commands.push_back(std::make_unique<SetCornerRadiusCommand>("p3", 1.0));
    commands.push_back(std::make_unique<SplitWallCommand>("w1", kPi));
    commands.push_back(std::make_unique<SetWallBulgeCommand>("w2", 1.0));
    const Project before = p;
    for (auto& c : commands) applyOk(*c, p);
    for (std::size_t i = commands.size(); i-- > 0;) commands[i]->undo(p);
    REQUIRE(p == before);
}

TEST_CASE("thickness and point moves cannot push a curved wall past its limits", "[core][wallgeometry][command]") {
    Project p = squareProject();
    SetWallBulgeCommand bend("w1", 2.0);
    applyOk(bend, p);

    SECTION("a thickness that would swallow the arc is refused") {
        SetWallThicknessCommand tooThick("w1", 4.5);
        applyRefused(tooThick, p, ErrorCode::InvalidValue);
        SetWallThicknessCommand fine("w1", 3.9);
        applyOk(fine, p);
    }
    SECTION("moving a point so the bulge passes a half circle is refused") {
        MovePointCommand shorter("p2", 2.0, 0.0);
        applyRefused(shorter, p, ErrorCode::InvalidValue);
        MovePointCommand longer("p2", 5.0, 0.0);
        applyOk(longer, p);
    }
    SECTION("moving a point so the radius gets too small for the thickness is refused") {
        findWall(p.plan.floors[0], "w1")->thickness = 3.0;
        MovePointCommand closer("p2", 3.2, 0.0);
        applyRefused(closer, p, ErrorCode::InvalidValue);
    }
    SECTION("straight walls and other walls are not affected") {
        MovePointCommand other("p3", 4.0, 6.0);
        applyOk(other, p);
        SetWallThicknessCommand thick("w2", 5.0);
        applyOk(thick, p);
    }
}

TEST_CASE("a thick wall on a tight arc has a clean band", "[core][wallgeometry]") {
    Floor f = squareFloor();
    f.walls[0].bulge = 2.0;
    f.walls[0].thickness = 3.8;
    const auto band = wallBand(f, f.walls[0]);
    REQUIRE_FALSE(band.empty());
    for (const auto& ring : band) {
        REQUIRE(ring.size() >= 3);
        REQUIRE(signedArea(ring) > 0.0);
        // No two edges that are not neighbours cross.
        const std::size_t n = ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 2; j < n; ++j) {
                if (i == 0 && j == n - 1) continue;
                const Vec2 a = ring[i], b = ring[(i + 1) % n], c = ring[j], d = ring[(j + 1) % n];
                const auto cr = [](Vec2 p, Vec2 q, Vec2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
                const bool crossing = cr(a, b, c) * cr(a, b, d) < -1e-12 && cr(c, d, a) * cr(c, d, b) < -1e-12;
                REQUIRE_FALSE(crossing);
            }
        }
    }
}

namespace {

// Floor with an L of two walls: w1 from (0,0) to (4,0) and w2 from (4,0) to (4,4), both 0.2 thick.
Floor cornerOfTwoWalls()
{
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 4.0, 0.0}};
    for (int i = 0; i < 2; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = "p" + std::to_string(i + 1);
        w.to = "p" + std::to_string(i + 2);
        w.thickness = 0.2;
        f.walls.push_back(w);
    }
    return f;
}

bool hasVertex(const std::vector<Vec2>& ring, Vec2 p)
{
    return std::any_of(ring.begin(), ring.end(), [&](Vec2 q) { return distance(p, q) < 1e-6; });
}

}

TEST_CASE("wallOutline mitres the ends of two walls at a right angle", "[core][wallgeometry]") {
    const Floor f = cornerOfTwoWalls();
    const auto first = wallOutline(f, f.walls[0]);
    const auto second = wallOutline(f, f.walls[1]);
    REQUIRE(first.size() == 1);
    REQUIRE(second.size() == 1);
    REQUIRE(signedArea(first[0]) > 0.0);
    REQUIRE(signedArea(second[0]) > 0.0);

    REQUIRE(hasVertex(first[0], {4.1, -0.1}));
    REQUIRE(hasVertex(second[0], {4.1, -0.1}));
    REQUIRE(hasVertex(first[0], {3.9, 0.1}));
    REQUIRE(hasVertex(second[0], {3.9, 0.1}));
    REQUIRE_FALSE(hasVertex(first[0], {4.1, 0.1}));
    REQUIRE(signedArea(first[0]) == Approx(0.82).margin(1e-9));

    const auto square = wallBand(f, f.walls[0]);
    REQUIRE(hasVertex(square[0], {4.1, 0.1}));
}

TEST_CASE("wallOutline equals wallBand at a T junction and for walls of different heights", "[core][wallgeometry]") {
    Floor t;
    t.id = "f1";
    t.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 2.0, 0.0, 0.0}, {"p4", 2.0, 3.0, 0.0}};
    const char* ends[3][2] = {{"p1", "p3"}, {"p3", "p2"}, {"p3", "p4"}};
    for (int i = 0; i < 3; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        t.walls.push_back(w);
    }
    for (const Wall& w : t.walls) REQUIRE(wallOutline(t, w) == wallBand(t, w));

    Floor corner = cornerOfTwoWalls();
    corner.walls[1].height = 2.0;
    REQUIRE(wallOutline(corner, corner.walls[0]) == wallBand(corner, corner.walls[0]));
    REQUIRE(wallOutline(corner, corner.walls[1]) == wallBand(corner, corner.walls[1]));
}
