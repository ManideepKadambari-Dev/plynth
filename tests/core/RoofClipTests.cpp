#include "RoofShapeChecks.h"

#include <plynth/core/Roofs.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using namespace plynth::test::roofshape;
using Catch::Approx;

namespace {

const std::vector<double> kOneLevel{0.0};

// Sets the plan's roof default to a type at 30 degrees with an overhang and a break at half the rise.
void roofWith(Plan& plan, const std::string& type, double overhang)
{
    plan.roof.type = type;
    plan.roof.pitch = 30.0;
    plan.roof.overhang = overhang;
    plan.roof.breakRatio = 0.5;
}

// Builds the roof of the plan's one floor and checks it the way every roof type is checked: one section of the
// type with no problems, closed planar faces, the covering adding up to the eaves, closed convex hulls.
FloorRoofs checkedRoof(const Plan& plan, const std::string& type, double overhang)
{
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == type);
    CHECK(r.problems.empty());
    checkFaces(r.roofs);
    checkCoversEaves(plan, 0, r.roofs, overhang);
    checkHulls(plan, 0, r.roofs);
    return r;
}

// The number of points of a face where its outline really turns.
int cornerCount(const RoofFace& f)
{
    int corners = 0;
    const std::size_t n = f.outline.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& a = f.outline[(i + n - 1) % n];
        const Vec3& b = f.outline[i];
        const Vec3& c = f.outline[(i + 1) % n];
        const double x = (b.y - a.y) * (c.z - b.z) - (b.z - a.z) * (c.y - b.y);
        const double y = (b.z - a.z) * (c.x - b.x) - (b.x - a.x) * (c.z - b.z);
        const double z = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (std::sqrt(x * x + y * y + z * z) > 1e-9) ++corners;
    }
    return corners;
}

// The lowest and highest point of a face.
std::pair<double, double> heightsOf(const RoofFace& f)
{
    double lo = 1e18;
    double hi = -1e18;
    for (const Vec3& p : f.outline) {
        lo = std::min(lo, p.z);
        hi = std::max(hi, p.z);
    }
    return {lo, hi};
}

// Says whether every point of a face has x at value.
bool standsAtX(const RoofFace& f, double value)
{
    return std::all_of(f.outline.begin(), f.outline.end(), [value](const Vec3& p) { return std::fabs(p.x - value) < 1e-6; });
}

}

TEST_CASE("jerkinhead and dutch gable are registered with the fields they use", "[roofs][clipped]")
{
    const RoofType* jerkinhead = findRoofType("jerkinhead");
    const RoofType* dutch = findRoofType("dutch_gable");
    REQUIRE(jerkinhead);
    REQUIRE(dutch);
    CHECK(jerkinhead->sortKey() == 36);
    CHECK(dutch->sortKey() == 34);
    CHECK(jerkinhead->usesPitch());
    CHECK(jerkinhead->usesDirection());
    CHECK(jerkinhead->usesOverhang());
    CHECK(jerkinhead->usesBreakRatio());
    CHECK_FALSE(jerkinhead->usesParapet());
    CHECK(dutch->usesPitch());
    CHECK_FALSE(dutch->usesDirection());
    CHECK(dutch->usesOverhang());
    CHECK(dutch->usesBreakRatio());
    CHECK_FALSE(dutch->usesSecondPitch());

    std::vector<std::string> ids;
    for (const RoofType* type : allRoofTypes()) ids.emplace_back(type->id());
    const auto at = [&](const char* id) { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
    CHECK(at("hip") < at("dutch_gable"));
    CHECK(at("dutch_gable") < at("jerkinhead"));
    CHECK(at("jerkinhead") < at("shed"));

    RoofSettings settings;
    settings.type = "jerkinhead";
    CHECK(checkRoofSettings(settings).ok());
    settings.type = "dutch_gable";
    CHECK(checkRoofSettings(settings).ok());
}

TEST_CASE("a jerkinhead on a rectangle clips both gable ends at half the rise", "[roofs][clipped]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "jerkinhead", 0.0);
    const FloorRoofs r = checkedRoof(plan, "jerkinhead", 0.0);

    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    REQUIRE(covering.size() == 4);
    int triangles = 0;
    for (const RoofFace& f : covering) {
        if (cornerCount(f) == 3) ++triangles;
    }
    CHECK(triangles == 2);
    const double ridge = 3.2 + 2.0 * kTan30;
    CHECK(topOf(covering) == Approx(ridge).margin(1e-6));
    // Each clip rises from the end at 3.2 + tan 30 to the ridge 1 m in, so the ridge runs from x 1 to x 5.
    CHECK(lengthAt(covering, ridge) == Approx(4.0).margin(1e-6));

    const std::vector<RoofFace> gables = facesOf(r.roofs, RoofPart::Gable);
    REQUIRE(gables.size() == 2);
    for (const RoofFace& f : gables) {
        CHECK((standsAtX(f, 0.0) || standsAtX(f, 6.0)));
        CHECK(heightsOf(f).first == Approx(3.2).margin(1e-9));
        CHECK(heightsOf(f).second == Approx(3.2 + kTan30 - kRoofThickness).margin(1e-6));
    }
}

TEST_CASE("a jerkinhead with an overhang covers its eaves", "[roofs][clipped]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "jerkinhead", 0.3);
    const FloorRoofs r = checkedRoof(plan, "jerkinhead", 0.3);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(topOf(covering) == Approx(3.2 + 2.0 * kTan30).margin(1e-6));
    CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 2);
}

TEST_CASE("a jerkinhead roofs L, T and U sections", "[roofs][clipped]")
{
    for (Plan plan : {lPlan(), tPlan(), uPlan()}) {
        roofWith(plan, "jerkinhead", 0.3);
        const FloorRoofs r = checkedRoof(plan, "jerkinhead", 0.3);
        CHECK_FALSE(facesOf(r.roofs, RoofPart::Gable).empty());
    }
}

TEST_CASE("a dutch gable on a rectangle has a hip below and a gablet at each end", "[roofs][clipped]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "dutch_gable", 0.0);
    const FloorRoofs r = checkedRoof(plan, "dutch_gable", 0.0);

    const double ridge = 3.2 + 2.0 * kTan30;
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(topOf(covering) == Approx(ridge).margin(1e-6));
    // The hip ends are cut 1 m in, at half their rise, and the ridge runs out to the cuts.
    CHECK(lengthAt(covering, ridge) == Approx(4.0).margin(1e-6));

    const std::vector<RoofFace> gables = facesOf(r.roofs, RoofPart::Gable);
    REQUIRE(gables.size() == 2);
    int west = 0;
    int east = 0;
    for (const RoofFace& f : gables) {
        const Vec3 n = newellOf(f.outline);
        if (standsAtX(f, 1.0)) {
            ++west;
            CHECK(n.x < 0.0);
        }
        if (standsAtX(f, 5.0)) {
            ++east;
            CHECK(n.x > 0.0);
        }
        CHECK(heightsOf(f).first == Approx(3.2 + kTan30).margin(1e-6));
        CHECK(heightsOf(f).second == Approx(ridge - kRoofThickness).margin(1e-6));
    }
    CHECK(west == 1);
    CHECK(east == 1);
    // The upper slopes end over each gablet with an edge face.
    int over = 0;
    for (const RoofFace& f : facesOf(r.roofs, RoofPart::Edge)) {
        if (standsAtX(f, 1.0) || standsAtX(f, 5.0)) ++over;
    }
    CHECK(over >= 2);
}

TEST_CASE("a dutch gable with an overhang covers its eaves", "[roofs][clipped]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "dutch_gable", 0.3);
    const FloorRoofs r = checkedRoof(plan, "dutch_gable", 0.3);
    CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 2);
    CHECK(bottomOf(facesOf(r.roofs, RoofPart::Covering)) == Approx(3.2 - 0.3 * kTan30).margin(1e-6));
}

TEST_CASE("a dutch gable on a square stays hipped", "[roofs][clipped]")
{
    Plan plan = makeNewProject().plan;
    addBoxRoom(plan.floors[0], 1, 0.0, 0.0, 4.0, 4.0);
    roofWith(plan, "dutch_gable", 0.3);
    const FloorRoofs r = checkedRoof(plan, "dutch_gable", 0.3);
    CHECK(facesOf(r.roofs, RoofPart::Gable).empty());
    CHECK(facesOf(r.roofs, RoofPart::Covering).size() == 4);
}

TEST_CASE("a dutch gable puts a gablet on every hip end of L, T and U sections", "[roofs][clipped]")
{
    // The hip of an L has two hip ends (one per arm), a T three, a U two (the tips of its arms).
    const std::vector<std::pair<Plan, std::size_t>> cases{{lPlan(), 2}, {tPlan(), 3}, {uPlan(), 2}};
    for (auto [plan, ends] : cases) {
        roofWith(plan, "dutch_gable", 0.3);
        const FloorRoofs r = checkedRoof(plan, "dutch_gable", 0.3);
        const std::vector<RoofFace> gables = facesOf(r.roofs, RoofPart::Gable);
        CHECK(gables.size() == ends);
        for (const RoofFace& f : gables) {
            CHECK(heightsOf(f).first == Approx(3.2 + kTan30).margin(1e-6));
            CHECK(heightsOf(f).second == Approx(3.2 + 2.0 * kTan30 - kRoofThickness).margin(1e-6));
        }
    }
}
