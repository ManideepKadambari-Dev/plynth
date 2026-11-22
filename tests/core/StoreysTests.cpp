#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/Storeys.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// Adds a room made of the four walls of rectangle n.
void addRoomOf(Floor& f, int n, const std::string& id)
{
    const std::string s = std::to_string(n);
    Room r;
    r.id = id;
    r.walls = {"w" + s + "1", "w" + s + "2", "w" + s + "3", "w" + s + "4"};
    f.rooms.push_back(r);
}

// The sum of the areas of the pieces.
double totalArea(const std::vector<PlanArea>& pieces)
{
    double sum = 0.0;
    for (const PlanArea& a : pieces) sum += planAreaSize(a);
    return sum;
}

// A plan with a lower floor of a closed square room (centre lines 0 to 4, walls 0.2) and an empty upper floor.
Plan twoFloors()
{
    Plan plan = makeNewProject().plan;
    Floor& f = plan.floors[0];
    addRectangle(f, 1, 0, 0, 4, 4, 0.2);
    addRoomOf(f, 1, "r1");
    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    plan.floors.push_back(upper);
    return plan;
}

}

TEST_CASE("a closed square room has one footprint piece without a hole", "[storeys]")
{
    const Plan plan = twoFloors();
    const std::vector<PlanArea> fp = floorFootprint(plan.floors[0]);
    REQUIRE(fp.size() == 1);
    CHECK(fp[0].holes.empty());
    CHECK(planAreaSize(fp[0]) == Approx(17.64).margin(1e-6));
    CHECK(signedArea(fp[0].outline) > 0.0);
}

TEST_CASE("walls without a room keep the hole", "[storeys]")
{
    Plan plan = twoFloors();
    plan.floors[0].rooms.clear();
    const std::vector<PlanArea> fp = floorFootprint(plan.floors[0]);
    REQUIRE(fp.size() == 1);
    REQUIRE(fp[0].holes.size() == 1);
    CHECK(signedArea(fp[0].holes[0]) < 0.0);
    CHECK(planAreaSize(fp[0]) == Approx(17.64 - 3.8 * 3.8).margin(1e-6));
}

TEST_CASE("an empty floor has no footprint", "[storeys]")
{
    const Plan plan = twoFloors();
    CHECK(floorFootprint(plan.floors[1]).empty());
}

TEST_CASE("exposed area is the part with no floor above", "[storeys]")
{
    Plan plan = twoFloors();
    Floor& up = plan.floors[1];
    addRectangle(up, 2, 0, 0, 2, 4, 0.2);
    addRoomOf(up, 2, "r2");

    const std::vector<PlanArea> exposed = exposedArea(plan, 0);
    REQUIRE(exposed.size() == 1);
    CHECK(planAreaSize(exposed[0]) == Approx(2.0 * 4.2).margin(1e-6));
    for (const Vec2& p : exposed[0].outline) CHECK(p.x >= 2.1 - 1e-6);

    const std::vector<PlanArea> top = exposedArea(plan, 1);
    REQUIRE(top.size() == 1);
    CHECK(planAreaSize(top[0]) == Approx(planAreaSize(floorFootprint(up)[0])).margin(1e-6));
    CHECK(overhangArea(plan, 0).empty());
    CHECK(overhangArea(plan, 1).empty());
}

TEST_CASE("an upper floor that reaches past the lower one has an overhang", "[storeys]")
{
    Plan plan = twoFloors();
    Floor& up = plan.floors[1];
    addRectangle(up, 2, 0, 0, 5, 4, 0.2);
    addRoomOf(up, 2, "r2");

    const std::vector<PlanArea> over = overhangArea(plan, 0);
    REQUIRE(over.size() == 1);
    CHECK(planAreaSize(over[0]) == Approx(1.0 * 4.2).margin(1e-6));
    for (const Vec2& p : over[0].outline) CHECK(p.x >= 4.1 - 1e-6);
    CHECK(exposedArea(plan, 0).empty());
}

TEST_CASE("pieces come in a stable order", "[storeys]")
{
    Plan plan = twoFloors();
    Floor& f = plan.floors[0];
    addRectangle(f, 2, 10, 0, 12, 2, 0.2);
    addRoomOf(f, 2, "r2");
    addRectangle(f, 3, -10, -5, -8, -3, 0.2);
    addRoomOf(f, 3, "r3");
    const std::vector<PlanArea> a = floorFootprint(f);
    REQUIRE(a.size() == 3);
    CHECK(a[0].outline.front().y < a[1].outline.front().y);
    CHECK(a[1].outline.front().y <= a[2].outline.front().y);
    CHECK(a == floorFootprint(f));
    std::reverse(f.walls.begin(), f.walls.end());
    CHECK(floorFootprint(f).size() == 3);
}

TEST_CASE("floor rise adds the slab to the ceiling", "[storeys]")
{
    Floor f;
    f.ceilingHeight = 2.8;
    f.slabThickness = 0.25;
    CHECK(floorRise(f) == Approx(3.05));
}

namespace {

// The area of a world polygon: its outline minus its holes.
double worldArea(const WorldPolygon& poly)
{
    double sum = signedArea(poly.outline);
    for (const auto& h : poly.holes) sum += signedArea(h);
    return sum;
}

// A spiral stair on the lower floor of twoFloors().
void addSpiral(Plan& plan)
{
    Stair s;
    s.id = "s1";
    s.type = "spiral";
    s.x = 2.5;
    s.y = 2.0;
    plan.floors[0].stairs.push_back(s);
}

}

TEST_CASE("the slab covers the floor and cuts a hole for a stair", "[storeys]")
{
    Plan plan = twoFloors();
    addSpiral(plan);
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    CHECK(slab.floor == "f1");
    CHECK(slab.bottom == Approx(3.0));
    CHECK(slab.top == Approx(3.2));
    REQUIRE(slab.polygons.size() == 1);
    REQUIRE(slab.polygons[0].holes.size() == 1);
    CHECK(signedArea(slab.polygons[0].outline) == Approx(17.64).margin(1e-6));
    CHECK(signedArea(slab.polygons[0].holes[0]) < 0.0);
    const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
    REQUIRE(layout.status.ok());
    CHECK(-signedArea(slab.polygons[0].holes[0]) == Approx(signedArea(layout.footprint)).margin(1e-6));
    CHECK(slab.undersides.empty());
    double lowest = 1e9;
    for (const Vec2& p : slab.polygons[0].outline) lowest = std::min(lowest, p.y);
    CHECK(lowest == Approx(-4.1).margin(1e-9));
}

TEST_CASE("the slab undersides are the overhang without the holes", "[storeys]")
{
    Plan plan = twoFloors();
    addSpiral(plan);
    Floor& upper = plan.floors[1];
    addRectangle(upper, 2, 0.0, 0.0, 6.0, 4.0, 0.2);
    addRoomOf(upper, 2, "r2");
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    const std::vector<PlanArea> over = overhangArea(plan, 0);
    REQUIRE_FALSE(over.empty());
    const double overhang = totalArea(over);
    double undersides = 0.0;
    for (const WorldPolygon& w : slab.undersides) undersides += worldArea(w);
    CHECK(undersides == Approx(overhang).margin(1e-6));

    const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
    double covered = 0.0;
    for (const WorldPolygon& w : slab.polygons) covered += worldArea(w);
    CHECK(covered == Approx(totalArea(floorFootprint(plan.floors[0])) + overhang - signedArea(layout.footprint)).margin(1e-6));
}

TEST_CASE("the slab of the top floor has no underside and a missing floor has no slab", "[storeys]")
{
    const Plan plan = twoFloors();
    const SlabBuild top = slabBuild(plan, 1, {0.0, 3.2});
    CHECK(top.polygons.empty());
    CHECK(top.undersides.empty());
    CHECK(top.bottom == Approx(6.2));
    CHECK(slabBuild(plan, 7, {0.0, 3.2}).polygons.empty());
}

namespace {

// Checks that sides has one entry for every edge of every ring of the slab's polygons.
void requireSidesMatch(const SlabBuild& slab)
{
    REQUIRE(slab.sides.size() == slab.polygons.size());
    for (std::size_t i = 0; i < slab.polygons.size(); ++i) {
        CHECK(slab.sides[i].outline.size() == slab.polygons[i].outline.size());
        REQUIRE(slab.sides[i].holes.size() == slab.polygons[i].holes.size());
        for (std::size_t h = 0; h < slab.polygons[i].holes.size(); ++h) {
            CHECK(slab.sides[i].holes[h].size() == slab.polygons[i].holes[h].size());
        }
    }
}

// Counts the entries of a ring's sides that have one kind.
std::size_t countOf(const std::vector<SlabSide>& sides, SlabSide kind)
{
    return static_cast<std::size_t>(std::count(sides.begin(), sides.end(), kind));
}

// Says whether the edge of a world ring from point i lies on the border of the 4.2 m square slab of twoFloors()
// (x from -0.1 to 4.1, world y from -4.1 to 0.1).
bool edgeOnSquare(const std::vector<Vec2>& ring, std::size_t i)
{
    const Vec2 a = ring[i];
    const Vec2 b = ring[(i + 1) % ring.size()];
    const auto both = [&](auto get, double value) { return std::abs(get(a) - value) < 1e-6 && std::abs(get(b) - value) < 1e-6; };
    const auto x = [](const Vec2& p) { return p.x; };
    const auto y = [](const Vec2& p) { return p.y; };
    return both(x, -0.1) || both(x, 4.1) || both(y, -4.1) || both(y, 0.1);
}

}

TEST_CASE("slabSideName names both kinds", "[storeys]")
{
    CHECK(slabSideName(SlabSide::Edge) == "edge");
    CHECK(slabSideName(SlabSide::Well) == "well");
}

TEST_CASE("a plain slab has an edge on every side", "[storeys]")
{
    const Plan plan = twoFloors();
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    requireSidesMatch(slab);
    REQUIRE(slab.sides.size() == 1);
    CHECK(slab.sides[0].outline.size() == slab.polygons[0].outline.size());
    CHECK(countOf(slab.sides[0].outline, SlabSide::Edge) == slab.sides[0].outline.size());
}

TEST_CASE("a courtyard's hole is all edge", "[storeys]")
{
    Plan plan = makeNewProject().plan;
    addRectangle(plan.floors[0], 1, 0, 0, 4, 4, 0.2);
    Floor upper;
    upper.id = "f2";
    plan.floors.push_back(upper);
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    requireSidesMatch(slab);
    REQUIRE(slab.polygons.size() == 1);
    REQUIRE(slab.polygons[0].holes.size() == 1);
    CHECK(countOf(slab.sides[0].outline, SlabSide::Edge) == slab.sides[0].outline.size());
    CHECK(countOf(slab.sides[0].holes[0], SlabSide::Edge) == slab.sides[0].holes[0].size());
}

TEST_CASE("an inner stair well is all well and its outline all edge", "[storeys]")
{
    Plan plan = twoFloors();
    addSpiral(plan);
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    requireSidesMatch(slab);
    REQUIRE(slab.polygons.size() == 1);
    REQUIRE(slab.polygons[0].holes.size() == 1);
    CHECK(countOf(slab.sides[0].outline, SlabSide::Edge) == slab.sides[0].outline.size());
    CHECK(countOf(slab.sides[0].holes[0], SlabSide::Well) == slab.sides[0].holes[0].size());
}

TEST_CASE("a well that reaches the boundary has well on its faces and edge on the rest", "[storeys]")
{
    Plan plan = twoFloors();
    addSpiral(plan);
    plan.floors[0].stairs[0].x = 4.0;
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    requireSidesMatch(slab);
    REQUIRE(slab.polygons.size() == 1);
    CHECK(slab.polygons[0].holes.empty());
    const std::vector<Vec2>& ring = slab.polygons[0].outline;
    std::size_t wells = 0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const bool onBorder = edgeOnSquare(ring, i);
        CHECK((slab.sides[0].outline[i] == SlabSide::Edge) == onBorder);
        if (!onBorder) ++wells;
    }
    CHECK(wells >= 1);
    CHECK(countOf(slab.sides[0].outline, SlabSide::Well) == wells);
    CHECK(countOf(slab.sides[0].outline, SlabSide::Edge) >= 4);
}

TEST_CASE("a straight stair flush with the boundary splits the sides where the kinds change", "[storeys]")
{
    Plan plan = twoFloors();
    Stair s;
    s.id = "s1";
    s.type = "straight";
    s.x = 3.0;
    s.y = 2.0;
    plan.floors[0].stairs.push_back(s);
    const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
    REQUIRE(layout.status.ok());
    double maxX = -1e9;
    for (const Vec2& p : layout.footprint) maxX = std::max(maxX, p.x);
    plan.floors[0].stairs[0].x += 4.1 - maxX;
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    requireSidesMatch(slab);
    REQUIRE(slab.polygons.size() == 1);
    CHECK(slab.polygons[0].holes.empty());
    const std::vector<Vec2>& ring = slab.polygons[0].outline;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        CHECK((slab.sides[0].outline[i] == SlabSide::Edge) == edgeOnSquare(ring, i));
    }
    CHECK(countOf(slab.sides[0].outline, SlabSide::Well) >= 1);
}

TEST_CASE("a missing floor and the top floor have no sides", "[storeys]")
{
    const Plan plan = twoFloors();
    CHECK(slabBuild(plan, 7, {0.0, 3.2}).sides.empty());
    CHECK(slabBuild(plan, 1, {0.0, 3.2}).sides.empty());
}

namespace {

// The lower floor of twoFloors() under an upper floor whose rectangle runs from (x0, y0) to (x1, y1).
Plan upperRectangle(double x0, double y0, double x1, double y1)
{
    Plan plan = twoFloors();
    addRectangle(plan.floors[1], 2, x0, y0, x1, y1, 0.2);
    addRoomOf(plan.floors[1], 2, "r2");
    return plan;
}

// Checks that roof sections, ledges and the footprint above add up to the slab, and returns the section count.
std::size_t requireSlabParts(const Plan& plan)
{
    const SlabBuild slab = slabBuild(plan, 0, {0.0, 3.2});
    const std::vector<PlanArea> sections = roofSectionAreas(plan, 0);
    double ledges = 0.0;
    for (const WorldPolygon& w : slab.tops) ledges += worldArea(w);
    double whole = 0.0;
    for (const WorldPolygon& w : slab.polygons) whole += worldArea(w);
    CHECK(totalArea(sections) + ledges == Approx(totalArea(exposedArea(plan, 0))).margin(1e-6));
    CHECK(totalArea(sections) + ledges + totalArea(floorFootprint(plan.floors[1])) == Approx(whole).margin(1e-6));
    return sections.size();
}

}

TEST_CASE("a hairline mismatch between floors gets no roof section and a ledge instead", "[storeys]")
{
    for (const auto& [dx, dy] : std::vector<std::pair<double, double>>{{0.005, 0.0}, {0.01, 0.0}, {0.0, 0.005}, {0.005, 0.005}, {0.01, 0.01}}) {
        INFO("offset " << dx << " " << dy);
        const Plan plan = upperRectangle(dx, dy, 4.0 + dx, 4.0 + dy);
        CHECK(requireSlabParts(plan) == 0);
        CHECK(roofSectionAreas(plan, 1).size() == 1);
        CHECK_FALSE(slabBuild(plan, 0, {0.0, 3.2}).tops.empty());
    }
}

TEST_CASE("a one metre setback keeps its section with exact corners, also beside a hairline strip", "[storeys]")
{
    const Plan plain = upperRectangle(0.0, 0.0, 3.0, 4.0);
    CHECK(requireSlabParts(plain) == 1);
    CHECK(slabBuild(plain, 0, {0.0, 3.2}).tops.empty());

    const Plan terrace = upperRectangle(0.0, 0.0, 3.05, 4.0);
    CHECK(requireSlabParts(terrace) == 1);
    CHECK(slabBuild(terrace, 0, {0.0, 3.2}).tops.empty());

    const Plan joined = upperRectangle(0.0, 0.0, 3.0, 3.995);
    REQUIRE(requireSlabParts(joined) == 1);
    const std::vector<PlanArea> sections = roofSectionAreas(joined, 0);
    CHECK(planAreaSize(sections[0]) == Approx(1.0 * 4.2).margin(1e-6));
    const auto closeTo = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const Vec2& p : sections[0].outline) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
        CHECK((closeTo(p.x, 3.1) || closeTo(p.x, 4.1) || closeTo(p.y, -0.1) || closeTo(p.y, 4.1)));
    }
    CHECK(closeTo(minX, 3.1));
    CHECK(closeTo(maxX, 4.1));
    CHECK(closeTo(minY, -0.1));
    CHECK(closeTo(maxY, 4.1));
    CHECK_FALSE(slabBuild(joined, 0, {0.0, 3.2}).tops.empty());
}

TEST_CASE("a 0.2 m strip gets a roof section and a 0.15 by 0.3 stub does not", "[storeys]")
{
    const Plan strip = upperRectangle(0.0, 0.0, 3.8, 4.0);
    CHECK(requireSlabParts(strip) == 1);

    Plan stub = upperRectangle(0.0, 0.0, 4.0, 4.0);
    Floor& f = stub.floors[0];
    addPoint(f, "q1", 6.0, 0.0);
    addPoint(f, "q2", 6.0, 0.15);
    addWall(f, "wq", "q1", "q2", 0.15);
    const std::vector<PlanArea> exposed = exposedArea(stub, 0);
    REQUIRE_FALSE(exposed.empty());
    CHECK(roofSectionAreas(stub, 0).empty());
    CHECK(requireSlabParts(stub) == 0);
    CHECK_FALSE(slabBuild(stub, 0, {0.0, 3.2}).tops.empty());
}

TEST_CASE("an anchor on a ledge is unused", "[storeys]")
{
    Plan plan = upperRectangle(0.005, 0.0, 4.005, 4.0);
    RoofSection entry;
    entry.id = "rf1";
    entry.floor = "f1";
    entry.x = -0.0975;
    entry.y = 2.0;
    plan.roofSections.push_back(entry);
    CHECK(unusedRoofSections(plan) == std::vector<Id>{"rf1"});
    CHECK(roofAreasOf(plan, "f1").empty());
    CHECK(roofAreasOf(plan, "f2").size() == 1);
}

namespace {

// The distance from a point to the nearest segment of a ring.
double distanceToRing(const std::vector<Vec2>& ring, Vec2 p)
{
    double best = 1e18;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % ring.size()];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        const double t = len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
        best = std::min(best, std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy)));
    }
    return best;
}

// Checks that the footprint of the floor is one piece whose outer ring lies on the outer ring of the biggest
// wall solid (within 1e-6, both ways, with the same area) and has the given number of holes.
void requireFootprintOnWalls(const Floor& floor, std::size_t holes)
{
    const std::vector<PlanArea> footprint = floorFootprint(floor);
    REQUIRE(footprint.size() == 1);
    CHECK(footprint[0].holes.size() == holes);

    const std::vector<WallSolid> solids = wallSolids(floor);
    REQUIRE_FALSE(solids.empty());
    std::vector<Vec2> outer;
    double biggest = 0.0;
    for (const WallSolid& solid : solids) {
        std::vector<Vec2> ring;
        for (const Vec2& w : solid.outline.points) ring.push_back({w.x, -w.y});
        if (std::abs(signedArea(ring)) > biggest) {
            biggest = std::abs(signedArea(ring));
            outer = ring;
        }
    }
    CHECK(signedArea(footprint[0].outline) == Approx(biggest).margin(1e-6));
    for (const Vec2& p : footprint[0].outline) CHECK(distanceToRing(outer, p) < 1e-6);
    for (const Vec2& p : outer) CHECK(distanceToRing(footprint[0].outline, p) < 1e-6);
}

// A closed room on points around a ring, with walls of one thickness. Points p<n>1 up, walls w<n>1 up.
void addPolygonRoom(Floor& f, int n, const std::vector<Vec2>& corners, double thickness, const std::string& room)
{
    const std::string s = std::to_string(n);
    Room r;
    r.id = room;
    for (std::size_t i = 0; i < corners.size(); ++i) addPoint(f, "p" + s + std::to_string(i + 1), corners[i].x, corners[i].y);
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const std::string id = "w" + s + std::to_string(i + 1);
        addWall(f, id, "p" + s + std::to_string(i + 1), "p" + s + std::to_string((i + 1) % corners.size() + 1), thickness);
        r.walls.push_back(id);
    }
    f.rooms.push_back(r);
}

}

TEST_CASE("the footprint of an octagon lies on its wall solids", "[storeys][footprint]")
{
    Floor f;
    f.id = "f1";
    std::vector<Vec2> corners;
    for (int i = 0; i < 8; ++i) corners.push_back({4.0 + 3.0 * std::cos(i * std::numbers::pi / 4.0), 4.0 + 3.0 * std::sin(i * std::numbers::pi / 4.0)});
    addPolygonRoom(f, 1, corners, 0.3, "r1");
    requireFootprintOnWalls(f, 0);
}

TEST_CASE("the footprint of a 60 degree corner lies on its wall solids", "[storeys][footprint]")
{
    Floor f;
    f.id = "f1";
    addPolygonRoom(f, 1, {{0.0, 0.0}, {6.0, 0.0}, {3.0, 3.0 * std::sqrt(3.0)}}, 0.3, "r1");
    requireFootprintOnWalls(f, 0);
}

TEST_CASE("the footprint of a concave 135 degree corner lies on its wall solids", "[storeys][footprint]")
{
    Floor f;
    f.id = "f1";
    addPolygonRoom(f, 1, {{0.0, 0.0}, {6.0, 0.0}, {6.0, 3.0}, {3.0, 3.0}, {0.0, 6.0}}, 0.3, "r1");
    requireFootprintOnWalls(f, 0);
}

TEST_CASE("the footprint of walls of unequal thickness in line lies on its wall solids", "[storeys][footprint]")
{
    Floor f;
    f.id = "f1";
    addPoint(f, "a", 0, 0);
    addPoint(f, "b", 4, 0);
    addPoint(f, "c", 8, 0);
    addPoint(f, "d", 8, 5);
    addPoint(f, "e", 0, 5);
    addWall(f, "w1", "a", "b", 0.3);
    addWall(f, "w2", "b", "c", 0.2);
    addWall(f, "w3", "c", "d", 0.2);
    addWall(f, "w4", "d", "e", 0.3);
    addWall(f, "w5", "e", "a", 0.3);
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4", "w5"};
    f.rooms.push_back(r);
    requireFootprintOnWalls(f, 0);
}

TEST_CASE("the footprint of a room with an island keeps only the island's inside as a hole", "[storeys][footprint]")
{
    Floor f;
    f.id = "f1";
    addRectangle(f, 1, 0, 0, 8, 8, 0.3);
    addRectangle(f, 2, 3, 3, 5, 5, 0.2);
    Room r;
    r.id = "r1";
    r.walls = {"w11", "w12", "w13", "w14", "w21", "w22", "w23", "w24"};
    f.rooms.push_back(r);
    const RoomShape shape = roomShape(f, f.rooms[0]);
    REQUIRE(shape.closed);
    REQUIRE(shape.rings.size() == 2);
    requireFootprintOnWalls(f, 1);
    const std::vector<PlanArea> footprint = floorFootprint(f);
    CHECK(planAreaSize(footprint[0]) == Approx(8.3 * 8.3 - 1.8 * 1.8).margin(1e-6));
}

TEST_CASE("the footprint of the curved wall fixture lies on its wall solids", "[storeys][footprint]")
{
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.6}, {"p2", 4.0, 0.0, 0.0}, {"p3", 8.0, 0.0, 0.0},
                {"p4", 8.0, 4.0, 0.0}, {"p5", 4.0, 4.0, 0.0}, {"p6", 0.0, 4.0, 0.0}};
    const char* ends[7][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p5"}, {"p5", "p6"}, {"p6", "p1"}, {"p2", "p5"}};
    for (int i = 0; i < 7; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.2;
        if (w.id == "w3") w.bulge = -0.5;
        f.walls.push_back(w);
    }
    Room left;
    left.id = "r1";
    left.walls = {"w1", "w7", "w5", "w6"};
    Room right;
    right.id = "r2";
    right.walls = {"w2", "w3", "w4", "w7"};
    f.rooms = {left, right};
    requireFootprintOnWalls(f, 0);
}
