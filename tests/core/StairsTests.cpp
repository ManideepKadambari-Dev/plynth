#include <plynth/core/Geometry.h>
#include <plynth/core/Stairs.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace plynth;
using Catch::Approx;

namespace {

// Two floors of ceiling 3.0 and slab 0.2, with one stair on the lower floor.
Plan twoFloorPlan(const Stair& stair)
{
    Plan plan;
    Floor f1;
    f1.id = "f1";
    f1.ceilingHeight = 3.0;
    f1.slabThickness = 0.2;
    f1.stairs = {stair};
    Floor f2;
    f2.id = "f2";
    plan.floors = {f1, f2};
    return plan;
}

// A stair of a type at the origin.
Stair stairOf(const std::string& type, double rotation = 0.0, StairTurn turn = StairTurn::Right)
{
    Stair s;
    s.id = "s1";
    s.type = type;
    s.rotation = rotation;
    s.turn = turn;
    return s;
}

// The layout of a stair on the lower floor of a two floor plan.
StairLayout layoutOf(const Stair& stair)
{
    const Plan plan = twoFloorPlan(stair);
    return stairLayout(plan, plan.floors[0], stair);
}

// Smallest and largest x and y of a polygon.
struct Box {
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
};

Box boxOf(const std::vector<Vec2>& points)
{
    Box b;
    for (const Vec2& p : points) {
        b.x0 = std::min(b.x0, p.x);
        b.x1 = std::max(b.x1, p.x);
        b.y0 = std::min(b.y0, p.y);
        b.y1 = std::max(b.y1, p.y);
    }
    return b;
}

// Says whether every corner turn of a polygon goes the same way.
bool convex(const std::vector<Vec2>& p)
{
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Vec2 a = p[i];
        const Vec2 b = p[(i + 1) % p.size()];
        const Vec2 c = p[(i + 2) % p.size()];
        const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (std::fabs(signedArea(p)) > 0.0 && cross * signedArea(p) < -1e-12) return false;
    }
    return true;
}

}

TEST_CASE("the four built in stair types are listed by sort key", "[stairs]")
{
    const std::vector<const StairType*> types = allStairTypes();
    REQUIRE(types.size() >= 4);
    CHECK(types[0]->id() == kStraightStairId);
    CHECK(types[1]->id() == kLShapeStairId);
    CHECK(types[2]->id() == kUShapeStairId);
    CHECK(types[3]->id() == kSpiralStairId);
    CHECK_FALSE(types[0]->usesTurn());
    CHECK(types[1]->usesTurn());
    CHECK(findStairType("nothing") == nullptr);
}

TEST_CASE("a duplicate or malformed stair type is refused", "[stairs]")
{
    class Fake final : public StairType {
    public:
        explicit Fake(std::string_view id) : id_(id) {}
        std::string_view id() const override { return id_; }
        std::string_view name() const override { return "Fake"; }
        int sortKey() const override { return 5; }
        bool usesTurn() const override { return false; }
        StairShape shape(const StairInput&) const override { return {}; }

    private:
        std::string_view id_;
    };
    CHECK_FALSE(registerStairType(std::make_unique<Fake>("straight")));
    CHECK_FALSE(registerStairType(std::make_unique<Fake>("Bad Id")));
    CHECK_FALSE(registerStairType(nullptr));
}

TEST_CASE("stair part names round trip", "[stairs]")
{
    for (StairPart p : {StairPart::Step, StairPart::Landing, StairPart::Column}) {
        CHECK(stairPartFromName(stairPartName(p)) == p);
    }
    CHECK_FALSE(stairPartFromName("ramp").has_value());
}

TEST_CASE("a straight stair has 17 even risers and 16 treads", "[stairs]")
{
    const StairLayout l = layoutOf(stairOf("straight"));
    REQUIRE(l.status.ok());
    CHECK(l.stair == "s1");
    CHECK(l.fromFloor == "f1");
    CHECK(l.toFloor == "f2");
    CHECK(l.rise == Approx(3.2));
    CHECK(l.riserCount == 17);
    CHECK(l.riser == Approx(3.2 / 17.0));
    CHECK(l.riser <= kMaxRiser);
    CHECK(l.going == Approx(0.27));
    REQUIRE(l.steps.size() == 16);
    CHECK(l.length == Approx(4.32));
    CHECK(l.steps[0].top == Approx(0.18824).margin(1e-5));
    CHECK(l.steps[0].bottom == 0.0);
    CHECK(l.steps[15].top == Approx(16 * 3.2 / 17.0));
    const Box first = boxOf(l.steps[0].outline);
    CHECK(first.y0 == Approx(0.0).margin(1e-9));
    CHECK(first.y1 == Approx(0.27));
    CHECK(first.x1 - first.x0 == Approx(1.0));
    CHECK(l.arrival.y == Approx(4.32));
    CHECK(l.arrival.x == Approx(0.0).margin(1e-9));
}

TEST_CASE("a straight footprint is a rectangle turned by the rotation", "[stairs]")
{
    const StairLayout l = layoutOf(stairOf("straight"));
    CHECK(l.footprint.size() == 4);
    CHECK(signedArea(l.footprint) == Approx(4.32));
    const Box b = boxOf(l.footprint);
    CHECK(b.x1 - b.x0 == Approx(1.0));
    CHECK(b.y1 - b.y0 == Approx(4.32));
    CHECK(b.y0 == Approx(0.0).margin(1e-9));

    const StairLayout turned = layoutOf(stairOf("straight", 90.0));
    const Box t = boxOf(turned.footprint);
    CHECK(t.x1 == Approx(0.0).margin(1e-9));
    CHECK(t.x0 == Approx(-4.32));
    CHECK(t.y1 - t.y0 == Approx(1.0));
    CHECK(turned.arrival.x == Approx(-4.32));
    CHECK(signedArea(turned.footprint) > 0.0);
}

TEST_CASE("stairs use the stair position", "[stairs]")
{
    Stair s = stairOf("straight");
    s.x = 5.0;
    s.y = 2.0;
    const StairLayout l = layoutOf(s);
    const Box b = boxOf(l.footprint);
    CHECK(b.y0 == Approx(2.0));
    CHECK((b.x0 + b.x1) / 2.0 == Approx(5.0));
}

TEST_CASE("an L shaped stair turning right has its second flight along plus u", "[stairs]")
{
    // Rotation 180 makes the frame's u axis the plan's plus x axis.
    const StairLayout l = layoutOf(stairOf("l_shape", 180.0));
    REQUIRE(l.status.ok());
    REQUIRE(l.steps.size() == 16);
    CHECK(l.steps[8].part == StairPart::Landing);
    const Box landing = boxOf(l.steps[8].outline);
    CHECK(landing.x1 - landing.x0 == Approx(1.0));
    CHECK(landing.y1 - landing.y0 == Approx(1.0));
    CHECK(l.steps[8].top == Approx(9 * 3.2 / 17.0));
    // At rotation 180 the climb goes toward -y, so the landing sits at y from -2.16 to -3.16.
    CHECK(landing.y1 == Approx(-8 * 0.27));
    const Box last = boxOf(l.steps[15].outline);
    CHECK(last.x1 == Approx(0.5 + 7 * 0.27));
    CHECK(l.arrival.x == Approx(0.5 + 7 * 0.27));
    CHECK(l.steps[15].top == Approx(16 * 3.2 / 17.0));
    CHECK(l.length == Approx(15 * 0.27 + 1.0));
    for (const StairStep& step : l.steps) CHECK(signedArea(step.outline) > 0.0);
}

TEST_CASE("an L shaped stair turning left goes the other way", "[stairs]")
{
    const StairLayout right = layoutOf(stairOf("l_shape", 0.0, StairTurn::Right));
    const StairLayout left = layoutOf(stairOf("l_shape", 0.0, StairTurn::Left));
    REQUIRE(right.steps.size() == left.steps.size());
    CHECK(left.arrival.x == Approx(-right.arrival.x));
    CHECK(left.arrival.y == Approx(right.arrival.y));
    for (const StairStep& step : left.steps) CHECK(signedArea(step.outline) > 0.0);
    CHECK(signedArea(left.footprint) > 0.0);
}

TEST_CASE("a U shaped stair comes back beside the first flight", "[stairs]")
{
    const StairLayout l = layoutOf(stairOf("u_shape", 180.0));
    REQUIRE(l.status.ok());
    REQUIRE(l.steps.size() == 16);
    const Box firstFlight = boxOf(l.steps[0].outline);
    const Box secondFlight = boxOf(l.steps[9].outline);
    CHECK(l.steps[8].part == StairPart::Landing);
    const Box landing = boxOf(l.steps[8].outline);
    CHECK(landing.x1 - landing.x0 == Approx(2.1));
    CHECK(landing.y1 - landing.y0 == Approx(1.0));
    CHECK(secondFlight.x0 - firstFlight.x1 == Approx(0.1));
    CHECK(secondFlight.x1 - secondFlight.x0 == Approx(1.0));
    CHECK(l.steps[9].top == Approx(10 * 3.2 / 17.0));
    CHECK(l.steps[15].top == Approx(16 * 3.2 / 17.0));
    // The last tread of the second flight lies one tread past the start, and the arrival is next to it.
    CHECK(std::fabs(l.arrival.y) == Approx(0.27));
    CHECK(l.arrival.x == Approx(1.1));
    CHECK(signedArea(l.footprint) > 0.0);
}

TEST_CASE("a spiral stair has convex steps round a column", "[stairs]")
{
    const StairLayout l = layoutOf(stairOf("spiral"));
    REQUIRE(l.status.ok());
    REQUIRE(l.steps.size() == 17);
    for (std::size_t i = 0; i < l.steps.size(); ++i) {
        CHECK(signedArea(l.steps[i].outline) > 0.0);
        CHECK(convex(l.steps[i].outline));
    }
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(l.steps[i].part == StairPart::Step);
        CHECK(l.steps[i].top - l.steps[i].bottom == Approx(kSpiralTreadThickness));
    }
    const StairStep& column = l.steps[16];
    CHECK(column.part == StairPart::Column);
    CHECK(column.bottom == 0.0);
    CHECK(column.top == Approx(3.2));
    const Box c = boxOf(column.outline);
    CHECK((c.x1 - c.x0) / 2.0 == Approx(0.1).margin(0.005));
    CHECK((c.x0 + c.x1) / 2.0 == Approx(-0.6).margin(1e-9));
    CHECK(c.y0 + c.y1 == Approx(0.0).margin(1e-9));
}

TEST_CASE("a spiral footprint is a circle of radius 1.1", "[stairs]")
{
    const StairLayout l = layoutOf(stairOf("spiral"));
    REQUIRE(l.footprint.size() >= 8);
    const Vec2 centre{-0.6, 0.0};
    for (const Vec2& p : l.footprint) CHECK(distance(p, centre) == Approx(1.1).margin(0.005));
    CHECK(signedArea(l.footprint) == Approx(std::numbers::pi * 1.1 * 1.1).epsilon(0.01));
}

TEST_CASE("the walk line starts at the stair position", "[stairs]")
{
    for (const char* type : {"straight", "l_shape", "u_shape", "spiral"}) {
        const StairLayout l = layoutOf(stairOf(type));
        REQUIRE(l.walkLine.size() >= 2);
        CHECK(l.walkLine.front().x == Approx(0.0).margin(1e-9));
        CHECK(l.walkLine.front().y == Approx(0.0).margin(1e-9));
        CHECK(l.arrival == l.walkLine.back());
        CHECK(l.length > 3.0);
    }
}

TEST_CASE("a tall floor gets more risers and a low one at least two", "[stairs]")
{
    Plan plan = twoFloorPlan(stairOf("straight"));
    plan.floors[0].ceilingHeight = 0.2;
    plan.floors[0].slabThickness = 0.1;
    StairLayout low = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
    CHECK(low.riserCount == 2);
    CHECK(low.steps.size() == 1);

    plan.floors[0].ceilingHeight = 3.8;
    plan.floors[0].slabThickness = 0.2;
    StairLayout tall = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
    CHECK(tall.riserCount == 22);
    CHECK(tall.riser <= kMaxRiser);
}

TEST_CASE("a stair on the top floor has no floor above it", "[stairs]")
{
    Plan plan = twoFloorPlan(stairOf("straight"));
    plan.floors[1].stairs = {stairOf("straight")};
    const StairLayout l = stairLayout(plan, plan.floors[1], plan.floors[1].stairs[0]);
    CHECK(l.status.code == ErrorCode::InvalidValue);
    CHECK(l.status.message == "Stair s1 has no floor above it.");
    CHECK(l.steps.empty());
    CHECK(l.footprint.empty());
}

TEST_CASE("an unknown type lists the known ids", "[stairs]")
{
    const StairLayout l = layoutOf(stairOf("ladder"));
    CHECK(l.status.code == ErrorCode::InvalidValue);
    CHECK(l.status.message.find("ladder") != std::string::npos);
    CHECK(l.status.message.find("straight") != std::string::npos);
    CHECK(l.status.message.find("spiral") != std::string::npos);
    CHECK(l.steps.empty());
}

TEST_CASE("a width outside the range is refused by the layout", "[stairs]")
{
    Stair s = stairOf("straight");
    s.width = 0.3;
    CHECK(layoutOf(s).status.code == ErrorCode::InvalidValue);
    s.width = 3.5;
    CHECK(layoutOf(s).status.code == ErrorCode::InvalidValue);
}

TEST_CASE("stairsArrivingAt lists the stairs of the floor below", "[stairs]")
{
    const Plan plan = twoFloorPlan(stairOf("l_shape"));
    const std::vector<StairLayout> arriving = stairsArrivingAt(plan, "f2");
    REQUIRE(arriving.size() == 1);
    CHECK(arriving[0].stair == "s1");
    CHECK(arriving[0].fromFloor == "f1");
    CHECK(stairsArrivingAt(plan, "f1").empty());
    CHECK(stairsArrivingAt(plan, "nope").empty());
}

TEST_CASE("previewStairLayout works for a stair that is not in the plan", "[stairs]")
{
    Plan plan = twoFloorPlan(stairOf("straight"));
    plan.floors[0].stairs.clear();
    Stair s = stairOf("straight");
    s.id.clear();
    const StairLayout l = previewStairLayout(plan, plan.floors[0], s);
    CHECK(l.status.ok());
    CHECK(l.steps.size() == 16);
    plan.floors[0].stairs.clear();
    CHECK(previewStairLayout(plan, plan.floors[1], s).status.message == "This stair has no floor above it.");
}

TEST_CASE("stairProblems reports a missing floor above and an unknown type", "[stairs]")
{
    Plan plan = twoFloorPlan(stairOf("ladder"));
    plan.floors[1].stairs = {stairOf("straight")};
    const std::vector<StairProblem> below = stairProblems(plan, plan.floors[0]);
    REQUIRE(below.size() == 1);
    CHECK(below[0].code == "stair_type_unknown");
    CHECK(below[0].error);
    const std::vector<StairProblem> top = stairProblems(plan, plan.floors[1]);
    REQUIRE(top.size() == 1);
    CHECK(top[0].code == "stair_no_floor_above");
    CHECK(top[0].stair == "s1");
}

TEST_CASE("checkStair accepts a good stair and refuses bad values", "[stairs]")
{
    CHECK(checkStair(stairOf("spiral")).ok());

    Stair s = stairOf("ladder");
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);

    s = stairOf("straight");
    s.width = 0.5;
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);
    s.width = 3.1;
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);

    s = stairOf("straight");
    s.x = std::nan("");
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);

    s = stairOf("straight");
    s.rotation = 360.0;
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);
    s.rotation = -1.0;
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);

    s = stairOf("straight");
    s.material = "bad name!";
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);
    s.material = "oak_planks";
    CHECK(checkStair(s).ok());
}
