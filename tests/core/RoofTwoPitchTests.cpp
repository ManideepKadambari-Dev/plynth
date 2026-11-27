#include "RoofShapeChecks.h"

#include <plynth/core/Roofs.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using namespace plynth::test::roofshape;
using Catch::Approx;

namespace {

const std::vector<double> kOneLevel{0.0};
constexpr double kBase = 3.2;

// The tangent of an angle in degrees.
double tanOf(double degrees)
{
    return std::tan(degrees * std::numbers::pi / 180.0);
}

// The cosine of an angle in degrees.
double cosOf(double degrees)
{
    return std::cos(degrees * std::numbers::pi / 180.0);
}

// Sets the plan's roof default to a type with two pitches, a break ratio and an overhang.
void roofWith(Plan& plan, const std::string& type, double overhang, double pitch, double secondPitch, double breakRatio)
{
    plan.roof.type = type;
    plan.roof.pitch = pitch;
    plan.roof.secondPitch = secondPitch;
    plan.roof.breakRatio = breakRatio;
    plan.roof.overhang = overhang;
}

// Builds the roof of the plan's one floor and checks it the way every roof type is checked: one section of the
// type with no problems, closed planar faces, the covering adding up to the eaves, edges only on the eaves,
// closed convex hulls.
FloorRoofs checkedRoof(const Plan& plan, const std::string& type, double overhang)
{
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == type);
    CHECK(r.problems.empty());
    checkFaces(r.roofs);
    checkCoversEaves(plan, 0, r.roofs, overhang);
    checkEdgesOnEaves(plan, 0, r.roofs, overhang);
    checkHulls(plan, 0, r.roofs);
    return r;
}

// How much a face looks up: 1 for a level face, the cosine of its slope otherwise.
double upward(const RoofFace& f)
{
    const Vec3 n = newellOf(f.outline);
    return n.z / std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
}

// Checks every covering face that reaches down to low slopes at lowPitch and every one that reaches up to high
// slopes at highPitch.
void checkSlopes(const std::vector<RoofFace>& covering, double low, double lowPitch, double high, double highPitch)
{
    int lower = 0;
    int upper = 0;
    for (const RoofFace& f : covering) {
        if (std::fabs(bottomOf({f}) - low) < 1e-6) {
            ++lower;
            CHECK(upward(f) == Approx(cosOf(lowPitch)).margin(1e-6));
        }
        if (std::fabs(topOf({f}) - high) < 1e-6) {
            ++upper;
            CHECK(upward(f) == Approx(cosOf(highPitch)).margin(1e-6));
        }
    }
    CHECK(lower > 0);
    CHECK(upper > 0);
}

// Says whether one face has every point at plan y value (world y is the plan's y turned over).
bool standsAtY(const RoofFace& f, double value)
{
    return std::all_of(f.outline.begin(), f.outline.end(), [value](const Vec3& p) { return std::fabs(p.y - value) < 1e-6; });
}

}

TEST_CASE("gambrel, mansard, saltbox and bonnet are registered with the fields they use", "[roofs][twopitch]")
{
    struct Expected {
        const char* id;
        int sortKey;
        bool direction;
        bool secondPitch;
        bool breakRatio;
        bool ridgeOffset;
        const char* fallback;
    };
    const std::vector<Expected> expected{
        {"gambrel", 60, true, true, true, false, "flat"},
        {"mansard", 62, false, true, true, false, "gambrel"},
        {"saltbox", 64, true, false, false, true, "flat"},
        {"bonnet", 68, false, true, true, false, "gable"},
    };
    for (const Expected& e : expected) {
        INFO(e.id);
        const RoofType* type = findRoofType(e.id);
        REQUIRE(type);
        CHECK(type->sortKey() == e.sortKey);
        CHECK(type->usesPitch());
        CHECK(type->usesOverhang());
        CHECK_FALSE(type->usesParapet());
        CHECK_FALSE(type->usesRise());
        CHECK(type->usesDirection() == e.direction);
        CHECK(type->usesSecondPitch() == e.secondPitch);
        CHECK(type->usesBreakRatio() == e.breakRatio);
        CHECK(type->usesRidgeOffset() == e.ridgeOffset);
        CHECK(type->fallback() == e.fallback);

        RoofSettings settings;
        settings.type = e.id;
        CHECK(checkRoofSettings(settings).ok());
        settings.pitch = 2.0;
        CHECK_FALSE(checkRoofSettings(settings).ok());
    }

    std::vector<std::string> ids;
    for (const RoofType* type : allRoofTypes()) ids.emplace_back(type->id());
    const auto at = [&](const char* id) { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
    CHECK(at("sawtooth") < at("gambrel"));
    CHECK(at("gambrel") < at("mansard"));
    CHECK(at("mansard") < at("saltbox"));
    CHECK(at("saltbox") < at("bonnet"));
}

TEST_CASE("a gambrel on a rectangle has a steep lower and a shallow upper slope on each side", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "gambrel", 0.0, 60.0, 20.0, 0.5);
    const FloorRoofs r = checkedRoof(plan, "gambrel", 0.0);

    // The run is 2 m on each side; the lower slope covers the outer metre of it.
    const double curb = kBase + tanOf(60.0);
    const double ridge = curb + tanOf(20.0);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(covering.size() == 4);
    CHECK(topOf(covering) == Approx(ridge).margin(1e-6));
    CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
    CHECK(hasCorner(covering, 0.0, -1.0, curb));
    CHECK(hasCorner(covering, 6.0, -3.0, curb));
    CHECK(hasCorner(covering, 0.0, -2.0, ridge));
    CHECK(lengthAt(covering, curb) == Approx(6.0).margin(1e-6));
    checkSlopes(covering, kBase, 60.0, ridge, 20.0);

    const std::vector<RoofFace> gables = facesOf(r.roofs, RoofPart::Gable);
    REQUIRE(gables.size() == 2);
    for (const RoofFace& f : gables) {
        CHECK(bottomOf({f}) == Approx(kBase).margin(1e-9));
        CHECK(topOf({f}) == Approx(ridge - kRoofThickness).margin(1e-6));
        CHECK(hasCorner({f}, f.outline.front().x, -1.0, curb - kRoofThickness));
    }
}

TEST_CASE("a gambrel with an overhang carries its lower slope out to the eaves", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "gambrel", 0.3, 60.0, 20.0, 0.5);
    const FloorRoofs r = checkedRoof(plan, "gambrel", 0.3);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(bottomOf(covering) == Approx(kBase - 0.3 * tanOf(60.0)).margin(1e-6));
    CHECK(topOf(covering) == Approx(kBase + tanOf(60.0) + tanOf(20.0)).margin(1e-6));
    CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 2);
}

TEST_CASE("a gambrel roofs L, T and U sections", "[roofs][twopitch]")
{
    for (Plan plan : {lPlan(), tPlan(), uPlan()}) {
        roofWith(plan, "gambrel", 0.3, 60.0, 20.0, 0.4);
        const FloorRoofs r = checkedRoof(plan, "gambrel", 0.3);
        CHECK_FALSE(facesOf(r.roofs, RoofPart::Gable).empty());
    }
}

TEST_CASE("a saltbox on a rectangle has one long low side and one short high side", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "saltbox", 0.0, 30.0, 15.0, 0.5);
    plan.roof.ridgeOffset = 0.25;
    const FloorRoofs r = checkedRoof(plan, "saltbox", 0.0);

    // The ridge moves 1 m from the middle toward plan y 0: 3 m of run on the long side, 1 m on the short one.
    const double ridge = kBase + 3.0 * kTan30;
    const double shortEaves = kBase + 2.0 * kTan30;
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(covering.size() == 2);
    CHECK(topOf(covering) == Approx(ridge).margin(1e-6));
    CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
    CHECK(hasCorner(covering, 0.0, -1.0, ridge));
    CHECK(hasCorner(covering, 0.0, -4.0, kBase));
    CHECK(hasCorner(covering, 0.0, 0.0, shortEaves));

    // Two gable ends and the raised wall under the short side's eaves.
    const std::vector<RoofFace> gables = facesOf(r.roofs, RoofPart::Gable);
    CHECK(gables.size() == 3);
    int raised = 0;
    for (const RoofFace& f : gables) {
        if (!standsAtY(f, 0.0)) continue;
        ++raised;
        CHECK(bottomOf({f}) == Approx(kBase).margin(1e-9));
        CHECK(topOf({f}) == Approx(shortEaves - kRoofThickness).margin(1e-6));
    }
    CHECK(raised == 1);
}

TEST_CASE("a saltbox ridge offset below 0 moves the ridge the other way", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "saltbox", 0.3, 30.0, 15.0, 0.5);
    plan.roof.ridgeOffset = -0.25;
    const FloorRoofs r = checkedRoof(plan, "saltbox", 0.3);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(topOf(covering) == Approx(kBase + 3.0 * kTan30).margin(1e-6));
    CHECK(bottomOf(covering) == Approx(kBase - 0.3 * kTan30).margin(1e-6));
    CHECK(hasCorner(covering, -0.3, -3.0, kBase + 3.0 * kTan30));
}

TEST_CASE("a saltbox with no ridge offset is a gable", "[roofs][twopitch]")
{
    Plan saltbox = rectanglePlan();
    roofWith(saltbox, "saltbox", 0.3, 30.0, 15.0, 0.5);
    Plan gable = rectanglePlan();
    roofWith(gable, "gable", 0.3, 30.0, 15.0, 0.5);
    const FloorRoofs a = checkedRoof(saltbox, "saltbox", 0.3);
    const FloorRoofs b = checkedRoof(gable, "gable", 0.3);
    CHECK(topOf(facesOf(a.roofs, RoofPart::Covering)) == Approx(topOf(facesOf(b.roofs, RoofPart::Covering))).margin(1e-9));
    CHECK(facesOf(a.roofs, RoofPart::Covering).size() == facesOf(b.roofs, RoofPart::Covering).size());
    CHECK(facesOf(a.roofs, RoofPart::Gable).size() == facesOf(b.roofs, RoofPart::Gable).size());
}

TEST_CASE("a saltbox roofs L, T and U sections", "[roofs][twopitch]")
{
    for (Plan plan : {lPlan(), tPlan(), uPlan()}) {
        roofWith(plan, "saltbox", 0.3, 30.0, 15.0, 0.5);
        plan.roof.ridgeOffset = 0.25;
        const FloorRoofs r = checkedRoof(plan, "saltbox", 0.3);
        CHECK_FALSE(facesOf(r.roofs, RoofPart::Gable).empty());
    }
}

TEST_CASE("a mansard on a rectangle has a steep lower slope on all four sides below a level break", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "mansard", 0.0, 60.0, 20.0, 0.5);
    const FloorRoofs r = checkedRoof(plan, "mansard", 0.0);

    // The run is 2 m; the break is 1 m in from every side, and the upper hip has its ridge from x 2 to x 4.
    const double curb = kBase + tanOf(60.0);
    const double top = curb + tanOf(20.0);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(covering.size() == 8);
    CHECK(topOf(covering) == Approx(top).margin(1e-6));
    CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
    CHECK(hasCorner(covering, 1.0, -1.0, curb));
    CHECK(hasCorner(covering, 5.0, -1.0, curb));
    CHECK(hasCorner(covering, 1.0, -3.0, curb));
    CHECK(hasCorner(covering, 5.0, -3.0, curb));
    CHECK(lengthAt(covering, curb) == Approx(4.0).margin(1e-6));
    CHECK(lengthAt(covering, top) == Approx(2.0).margin(1e-6));
    checkSlopes(covering, kBase, 60.0, top, 20.0);
    CHECK(facesOf(r.roofs, RoofPart::Gable).empty());
}

TEST_CASE("a mansard with an overhang carries its lower slopes out to the eaves", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "mansard", 0.3, 60.0, 20.0, 0.5);
    const FloorRoofs r = checkedRoof(plan, "mansard", 0.3);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(bottomOf(covering) == Approx(kBase - 0.3 * tanOf(60.0)).margin(1e-6));
    CHECK(topOf(covering) == Approx(kBase + tanOf(60.0) + tanOf(20.0)).margin(1e-6));
    CHECK(facesOf(r.roofs, RoofPart::Gable).empty());
}

TEST_CASE("a mansard roofs L, T and U sections through the skeleton", "[roofs][twopitch]")
{
    // Every arm is 4 m wide, so the run is 2 m and the break 1 m in all round.
    const double curb = kBase + tanOf(60.0);
    const double top = curb + tanOf(20.0);
    for (Plan plan : {lPlan(), tPlan(), uPlan()}) {
        roofWith(plan, "mansard", 0.3, 60.0, 20.0, 0.5);
        const FloorRoofs r = checkedRoof(plan, "mansard", 0.3);
        const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
        CHECK(topOf(covering) == Approx(top).margin(1e-6));
        CHECK(lengthAt(covering, curb) > 4.0);
        checkSlopes(covering, kBase - 0.3 * tanOf(60.0), 60.0, top, 20.0);
        CHECK(facesOf(r.roofs, RoofPart::Gable).empty());
    }
}

TEST_CASE("a mansard with a level upper part has a flat top", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "mansard", 0.3, 70.0, 0.0, 0.5);
    const FloorRoofs r = checkedRoof(plan, "mansard", 0.3);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(topOf(covering) == Approx(kBase + tanOf(70.0)).margin(1e-6));
    CHECK(lengthAt(covering, kBase + tanOf(70.0)) == Approx(4.0).margin(1e-6));
}

TEST_CASE("a bonnet on a rectangle has a flatter kick at the eaves below the hip", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "bonnet", 0.0, 45.0, 15.0, 0.5);
    const FloorRoofs r = checkedRoof(plan, "bonnet", 0.0);

    const double curb = kBase + tanOf(15.0);
    const double top = curb + 1.0;
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(covering.size() == 8);
    CHECK(topOf(covering) == Approx(top).margin(1e-6));
    CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
    CHECK(hasCorner(covering, 1.0, -1.0, curb));
    CHECK(hasCorner(covering, 5.0, -3.0, curb));
    CHECK(lengthAt(covering, curb) == Approx(4.0).margin(1e-6));
    CHECK(lengthAt(covering, top) == Approx(2.0).margin(1e-6));
    checkSlopes(covering, kBase, 15.0, top, 45.0);
    CHECK(facesOf(r.roofs, RoofPart::Gable).empty());
}

TEST_CASE("a bonnet with an overhang carries its kick out to the eaves", "[roofs][twopitch]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "bonnet", 0.3, 45.0, 15.0, 0.3);
    const FloorRoofs r = checkedRoof(plan, "bonnet", 0.3);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(bottomOf(covering) == Approx(kBase - 0.3 * tanOf(15.0)).margin(1e-6));
    // The kick covers 0.6 m of the 2 m run, the hip the other 1.4 m.
    CHECK(topOf(covering) == Approx(kBase + 0.6 * tanOf(15.0) + 1.4).margin(1e-6));
}

TEST_CASE("a bonnet roofs L, T and U sections through the skeleton", "[roofs][twopitch]")
{
    const double top = kBase + tanOf(15.0) + 1.0;
    for (Plan plan : {lPlan(), tPlan(), uPlan()}) {
        roofWith(plan, "bonnet", 0.3, 45.0, 15.0, 0.5);
        const FloorRoofs r = checkedRoof(plan, "bonnet", 0.3);
        const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
        CHECK(topOf(covering) == Approx(top).margin(1e-6));
        checkSlopes(covering, kBase - 0.3 * tanOf(15.0), 15.0, top, 45.0);
        CHECK(facesOf(r.roofs, RoofPart::Gable).empty());
    }
}
