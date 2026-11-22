#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Storeys.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
