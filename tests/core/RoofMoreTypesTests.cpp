#include "RoofShapeChecks.h"

#include <plynth/core/Geometry.h>
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

// Sets the plan's roof default.
void roofWith(Plan& plan, const std::string& type, double overhang, double pitch = 30.0, double rise = 1.0)
{
    plan.roof.type = type;
    plan.roof.pitch = pitch;
    plan.roof.overhang = overhang;
    plan.roof.rise = rise;
}

// Builds the roof of the plan's one floor and checks it the way every roof type is checked: one section of the
// type with no problems, closed planar faces, the covering adding up to the eaves, closed convex hulls, and with
// edgesOnEaves the edges only on the eaves.
FloorRoofs checkedRoof(const Plan& plan, const std::string& type, double overhang, bool edgesOnEaves = true)
{
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == type);
    CHECK(r.problems.empty());
    checkFaces(r.roofs);
    checkCoversEaves(plan, 0, r.roofs, overhang);
    if (edgesOnEaves) checkEdgesOnEaves(plan, 0, r.roofs, overhang);
    checkHulls(plan, 0, r.roofs);
    return r;
}

// Builds the roof of the plan's one floor and checks it fell back to another type with one plain problem.
void checkFallsBack(const Plan& plan, const std::string& fallback)
{
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == fallback);
    REQUIRE(r.problems.size() == 1);
    CHECK(r.problems[0].code == "roof_type_fallback");
    CHECK_FALSE(r.problems[0].message.empty());
    checkFaces(r.roofs);
    checkHulls(plan, 0, r.roofs);
}

// The plans of a rectangle, an L, a T and a U, each with a name.
struct NamedPlan {
    std::string name;
    Plan plan;
};

std::vector<NamedPlan> allShapes()
{
    return {{"rectangle", rectanglePlan()}, {"L", lPlan()}, {"T", tPlan()}, {"U", uPlan()}};
}

// The middle of a face seen from above.
Vec2 middleOf(const RoofFace& face)
{
    Vec2 mid{0.0, 0.0};
    for (const Vec3& p : face.outline) mid = {mid.x + p.x, mid.y + p.y};
    const double count = static_cast<double>(face.outline.size());
    return {mid.x / count, mid.y / count};
}

// Says whether every point of a face lies at world y.
bool allAtY(const RoofFace& face, double y)
{
    return std::all_of(face.outline.begin(), face.outline.end(), [y](const Vec3& p) { return std::fabs(p.y - y) < 1e-6; });
}

}

TEST_CASE("the more roof types are registered with the fields they use", "[roofs][more]")
{
    struct Expected {
        const char* id;
        int sortKey;
        bool pitch;
        bool direction;
        bool parapet;
        bool ridgeOffset;
        bool rise;
        const char* fallback;
    };
    const std::vector<Expected> expected{
        {"green", 12, false, false, true, false, false, "flat"},
        {"butterfly", 66, true, true, false, true, false, "flat"},
        {"clerestory", 70, true, true, false, true, true, "shed"},
        {"a_frame", 72, true, true, false, false, false, "gable"},
        {"m_shaped", 74, true, true, false, false, false, "flat"},
        {"barrel", 80, false, true, false, false, true, "flat"},
        {"dome", 82, false, false, false, false, true, "flat"},
        {"bow", 84, false, true, false, false, true, "flat"},
    };
    for (const Expected& e : expected) {
        INFO(e.id);
        const RoofType* type = findRoofType(e.id);
        REQUIRE(type != nullptr);
        CHECK(type->sortKey() == e.sortKey);
        CHECK(type->usesPitch() == e.pitch);
        CHECK(type->usesDirection() == e.direction);
        CHECK(type->usesOverhang());
        CHECK(type->usesParapet() == e.parapet);
        CHECK(type->usesRidgeOffset() == e.ridgeOffset);
        CHECK(type->usesRise() == e.rise);
        CHECK_FALSE(type->usesSecondPitch());
        CHECK_FALSE(type->usesBreakRatio());
        CHECK(type->fallback() == e.fallback);
        CHECK_FALSE(type->name().empty());
    }
    std::vector<std::string> ids;
    for (const RoofType* type : allRoofTypes()) ids.emplace_back(type->id());
    const auto at = [&](const char* id) { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
    CHECK(at("flat") < at("green"));
    CHECK(at("green") < at("gable"));
    CHECK(at("shed") < at("butterfly"));
    CHECK(at("butterfly") < at("clerestory"));
    CHECK(at("clerestory") < at("a_frame"));
    CHECK(at("a_frame") < at("m_shaped"));
    CHECK(at("m_shaped") < at("barrel"));
    CHECK(at("barrel") < at("dome"));
    CHECK(at("dome") < at("bow"));

    RoofSettings settings;
    settings.type = "barrel";
    settings.pitch = 0.0;
    CHECK(checkRoofSettings(settings).ok());
    settings.type = "butterfly";
    CHECK_FALSE(checkRoofSettings(settings).ok());
}

TEST_CASE("butterfly, M-shaped, barrel, bow and green roofs on a rectangle, L, T and U are closed", "[roofs][more]")
{
    for (const char* type : {"butterfly", "m_shaped", "barrel", "bow", "green"}) {
        for (NamedPlan& shape : allShapes()) {
            for (const double overhang : {0.0, 0.3}) {
                INFO(type << " on " << shape.name << ", overhang " << overhang);
                roofWith(shape.plan, type, overhang);
                if (std::string(type) == "green") shape.plan.roof.parapet = 0.0;
                checkedRoof(shape.plan, type, overhang);
            }
        }
    }
}

TEST_CASE("roofs of the more types stay closed when the direction is turned", "[roofs][more]")
{
    for (const char* type : {"butterfly", "m_shaped", "clerestory", "barrel", "bow", "a_frame"}) {
        for (const double degrees : {30.0, 90.0, 135.0}) {
            INFO(type << " at " << degrees << " degrees");
            Plan plan = rectanglePlan();
            roofWith(plan, type, 0.3, std::string(type) == "a_frame" ? 60.0 : 30.0, 0.8);
            plan.roof.direction = degrees;
            const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
            REQUIRE(r.roofs.size() == 1);
            CHECK(r.roofs[0].type == type);
            CHECK(r.problems.empty());
            checkFaces(r.roofs);
            checkHulls(plan, 0, r.roofs);
            const Covering covering = coveringOf(r.roofs);
            CHECK(covering.sum == Approx(covering.united).margin(1e-4));
            if (std::string(type) != "a_frame") {
                CHECK(covering.sum == Approx(areaOf(eavesOf(plan, plan.floors[0].id, 0.3))).margin(1e-4));
            }
        }
    }
}

TEST_CASE("a butterfly roof falls to a valley that the ridge offset moves", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "butterfly", 0.0);
    {
        const FloorRoofs r = checkedRoof(plan, "butterfly", 0.0);
        const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
        CHECK(covering.size() == 2);
        CHECK(topOf(covering) == Approx(kBase + 2.0 * kTan30).margin(1e-6));
        CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
        CHECK(hasCorner(covering, 0.0, -2.0, kBase));
        CHECK(hasCorner(covering, 6.0, -2.0, kBase));
        // The long sides stand high, so each gets a fill under it; each end gets a V, parted where the valley's
        // underside dips below the base.
        CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 6);
    }
    // An offset above 0 moves the valley to the left of the direction, here +x, so toward world +y.
    plan.roof.ridgeOffset = 0.25;
    const FloorRoofs r = checkedRoof(plan, "butterfly", 0.0);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(hasCorner(covering, 0.0, -1.0, kBase));
    CHECK(topOf(covering) == Approx(kBase + 3.0 * kTan30).margin(1e-6));
    CHECK(hasCorner(covering, 0.0, -4.0, kBase + 3.0 * kTan30));
    CHECK(hasCorner(covering, 0.0, 0.0, kBase + kTan30));
}

TEST_CASE("an M-shaped roof has two ridges and a valley between them", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "m_shaped", 0.0);
    const FloorRoofs r = checkedRoof(plan, "m_shaped", 0.0);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(covering.size() == 4);
    CHECK(topOf(covering) == Approx(kBase + kTan30).margin(1e-6));
    CHECK(hasCorner(covering, 0.0, -1.0, kBase + kTan30));
    CHECK(hasCorner(covering, 0.0, -3.0, kBase + kTan30));
    CHECK(hasCorner(covering, 0.0, -2.0, kBase));
    CHECK(lengthAt(covering, kBase + kTan30) == Approx(6.0).margin(1e-6));
    // A W shaped fill at each end, in two parts where the valley's underside dips below the base, and none along
    // the low long sides.
    CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 4);
}

TEST_CASE("a clerestory roof has two sheds with a band of glass between them", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "clerestory", 0.3, 30.0, 1.0);
    const FloorRoofs r = checkedRoof(plan, "clerestory", 0.3, false);
    const double low = kBase + 2.0 * kTan30;
    const double high = low + 1.0 + kRoofThickness;
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(topOf(covering) == Approx(high).margin(1e-6));
    CHECK(hasCorner(covering, -0.3, -2.0, low));
    CHECK(hasCorner(covering, -0.3, -2.0, high));
    CHECK(hasCorner(covering, -0.3, 0.3, high - 2.3 * kTan30));

    const std::vector<RoofFace> glass = facesOf(r.roofs, RoofPart::Glazing);
    REQUIRE_FALSE(glass.empty());
    double length = 0.0;
    for (const RoofFace& face : glass) {
        CHECK(allAtY(face, -2.0));
        CHECK(bottomOf({face}) == Approx(low).margin(1e-6));
        CHECK(topOf({face}) == Approx(low + 1.0).margin(1e-6));
        length += lengthAt({face}, low);
    }
    CHECK(length == Approx(6.6).margin(1e-6));

    // Edges stand on the eaves or at the step; no fill stands under the step, so the rooms stay open to the glass.
    const Clipper2Lib::PathsD eaves = eavesOf(plan, plan.floors[0].id, 0.3);
    for (const RoofFace& face : facesOf(r.roofs, RoofPart::Edge)) {
        const Vec2 mid = middleOf(face);
        CHECK((distanceToOutline(eaves, mid) < 1e-4 || std::fabs(mid.y + 2.0) < 1e-6));
    }
    for (const RoofFace& face : facesOf(r.roofs, RoofPart::Gable)) CHECK_FALSE(allAtY(face, -2.0));
}

TEST_CASE("a clerestory roof on an L, T or U is built as a shed", "[roofs][more]")
{
    for (Plan shape : {lPlan(), tPlan(), uPlan()}) {
        roofWith(shape, "clerestory", 0.3);
        checkFallsBack(shape, "shed");
    }
}

TEST_CASE("a clerestory roof keeps its high shed above the base when the step is moved far", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "clerestory", 0.0, 30.0, 0.2);
    plan.roof.ridgeOffset = -0.45;
    const FloorRoofs r = checkedRoof(plan, "clerestory", 0.0, false);
    // The step lies 0.2 m in from the low side, so the high shed falls 3.8 m and the glass grows to keep it above.
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
    CHECK(hasCorner(covering, 0.0, 0.0, kBase));
    const std::vector<RoofFace> glass = facesOf(r.roofs, RoofPart::Glazing);
    REQUIRE_FALSE(glass.empty());
    CHECK(topOf(glass) - bottomOf(glass) > 0.2);
}

TEST_CASE("a barrel roof is an arc of flat strips standing on the walls", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "barrel", 0.0, 30.0, 1.0);
    const FloorRoofs r = checkedRoof(plan, "barrel", 0.0);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    const double radius = (4.0 + 1.0) / 2.0;
    const int segments = arcSegmentCount(radius, 2.0 * std::asin(2.0 / radius));
    CHECK(static_cast<int>(covering.size()) == segments);
    CHECK(topOf(covering) <= kBase + 1.0 + 1e-9);
    CHECK(topOf(covering) > kBase + 1.0 - 0.01);
    CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
    CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 2);
    // Every corner of the top lies on the circle through the walls' tops and the crown.
    for (const RoofFace& face : covering) {
        for (const Vec3& p : face.outline) {
            const double off = p.y + 2.0;
            const double z = p.z - (kBase + 1.0 - radius);
            CHECK(std::hypot(off, z) == Approx(radius).margin(1e-6));
        }
    }

    // With eaves the outer strips carry on, a little lower than the walls' tops.
    roofWith(plan, "barrel", 0.3, 30.0, 1.0);
    const FloorRoofs eaves = checkedRoof(plan, "barrel", 0.3);
    CHECK(bottomOf(facesOf(eaves.roofs, RoofPart::Covering)) < kBase);
}

TEST_CASE("a barrel or bow roof too high for its section is built flat", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "barrel", 0.3, 30.0, 1.6);
    checkFallsBack(plan, "flat");
    roofWith(plan, "barrel", 0.3, 30.0, 1.5);
    checkedRoof(plan, "barrel", 0.3);
    roofWith(plan, "bow", 0.3, 30.0, 3.1);
    checkFallsBack(plan, "flat");
    roofWith(plan, "bow", 0.3, 30.0, 3.0);
    checkedRoof(plan, "bow", 0.3);
}

TEST_CASE("a bow roof curves from the low side up to a level top on the high side", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "bow", 0.0, 30.0, 1.0);
    const FloorRoofs r = checkedRoof(plan, "bow", 0.0);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    // The ridge runs along x, so the roof falls toward plan +y, world -y.
    CHECK(hasCorner(covering, 0.0, -4.0, kBase));
    CHECK(hasCorner(covering, 0.0, 0.0, kBase + 1.0));
    CHECK(topOf(covering) == Approx(kBase + 1.0).margin(1e-6));
    const double radius = (16.0 + 1.0) / 2.0;
    CHECK(static_cast<int>(covering.size()) == arcSegmentCount(radius, std::asin(4.0 / radius)));
    // The strips get flatter toward the high side.
    double steepest = 0.0;
    double flattest = 1e18;
    for (const RoofFace& face : covering) {
        const Vec3 n = newellOf(face.outline);
        const double tilt = std::hypot(n.x, n.y) / std::fabs(n.z);
        const Vec2 mid = middleOf(face);
        if (mid.y < -3.5) steepest = tilt;
        if (mid.y > -0.5) flattest = tilt;
    }
    CHECK(steepest > flattest);
    // A wall fills under the high side, and each end gets a curved fill.
    CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 3);
}

TEST_CASE("a dome over a square rises to one apex in the middle", "[roofs][more]")
{
    Plan plan = makeNewProject().plan;
    addBoxRoom(plan.floors[0], 1, 0.0, 0.0, 6.0, 6.0);
    for (const double overhang : {0.0, 0.3}) {
        INFO("overhang " << overhang);
        roofWith(plan, "dome", overhang, 30.0, 1.5);
        const FloorRoofs r = checkedRoof(plan, "dome", overhang);
        const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
        CHECK(topOf(covering) == Approx(kBase + 1.5).margin(1e-4));
        CHECK(facesOf(r.roofs, RoofPart::Gable).empty());
        if (overhang == 0.0) {
            CHECK(bottomOf(covering) == Approx(kBase).margin(1e-6));
            const double radius = (9.0 + 2.25) / 3.0;
            const int rings = arcSegmentCount(radius, std::asin(3.0 / radius));
            CHECK(static_cast<int>(covering.size()) == 4 * rings);
        }
    }
}

TEST_CASE("a dome over a round room and a long rectangle is closed", "[roofs][more]")
{
    Plan round = makeNewProject().plan;
    std::vector<Vec2> points;
    for (int k = 0; k < 24; ++k) {
        const double a = 2.0 * std::numbers::pi * k / 24.0;
        points.push_back({5.0 + 4.0 * std::cos(a), 5.0 + 4.0 * std::sin(a)});
    }
    addOutlineRoom(round.floors[0], points);
    roofWith(round, "dome", 0.3, 30.0, 2.0);
    const FloorRoofs r = checkedRoof(round, "dome", 0.3);
    CHECK(topOf(facesOf(r.roofs, RoofPart::Covering)) == Approx(kBase + 2.0).margin(1e-4));

    Plan rectangle = rectanglePlan();
    roofWith(rectangle, "dome", 0.3, 30.0, 1.0);
    checkedRoof(rectangle, "dome", 0.3);
}

TEST_CASE("a dome refuses an outline with inward corners and a rise too high", "[roofs][more]")
{
    Plan l = lPlan();
    roofWith(l, "dome", 0.3, 30.0, 1.0);
    checkFallsBack(l, "flat");

    Plan square = makeNewProject().plan;
    addBoxRoom(square.floors[0], 1, 0.0, 0.0, 6.0, 6.0);
    roofWith(square, "dome", 0.3, 30.0, 3.0);
    checkFallsBack(square, "flat");
}

TEST_CASE("a green roof has a planting layer and an edge round it", "[roofs][more]")
{
    for (NamedPlan& shape : allShapes()) {
        INFO(shape.name);
        roofWith(shape.plan, "green", 0.3);
        shape.plan.roof.parapet = 0.0;
        const FloorRoofs bare = checkedRoof(shape.plan, "green", 0.3);
        const double top = kBase + kRoofThickness + 0.15;
        CHECK(topOf(facesOf(bare.roofs, RoofPart::Covering)) == Approx(top).margin(1e-9));
        CHECK(bottomOf(facesOf(bare.roofs, RoofPart::Covering)) == Approx(top).margin(1e-9));
        const std::vector<RoofFace> edge = facesOf(bare.roofs, RoofPart::Parapet);
        REQUIRE_FALSE(edge.empty());
        CHECK(topOf(edge) == Approx(top + 0.05).margin(1e-9));
        CHECK(bottomOf(edge) == Approx(top).margin(1e-9));
        CHECK(bottomOf(facesOf(bare.roofs, RoofPart::Edge)) == Approx(kBase).margin(1e-9));
        CHECK(topOf(facesOf(bare.roofs, RoofPart::Edge)) == Approx(top).margin(1e-9));

        // With a parapet the layer covers the section and the parapet stands on the planting.
        shape.plan.roof.parapet = 0.6;
        const FloorRoofs walled = checkedRoof(shape.plan, "green", 0.0);
        CHECK(topOf(facesOf(walled.roofs, RoofPart::Parapet)) == Approx(top + 0.6).margin(1e-9));
    }
}

TEST_CASE("an A-frame roof comes down near the ground across its ridge", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "a_frame", 0.3, 60.0);
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == "a_frame");
    CHECK(r.problems.empty());
    checkFaces(r.roofs);
    checkHulls(plan, 0, r.roofs);

    const double reach = (kBase - 0.3) / tanOf(60.0);
    const std::vector<RoofFace> covering = facesOf(r.roofs, RoofPart::Covering);
    CHECK(bottomOf(covering) == Approx(0.3).margin(1e-6));
    CHECK(topOf(covering) == Approx(kBase + 2.0 * tanOf(60.0)).margin(1e-6));
    CHECK(hasCorner(covering, -0.3, reach, 0.3));
    CHECK(hasCorner(covering, 6.3, -4.0 - reach, 0.3));
    const Covering measured = coveringOf(r.roofs);
    const double expected = 6.6 * (4.0 + 2.0 * reach);
    CHECK(measured.sum == Approx(expected).margin(1e-4));
    CHECK(measured.united == Approx(expected).margin(1e-4));
    // A triangle fills each end between the slab and the roof; the long walls stand under the slopes.
    CHECK(facesOf(r.roofs, RoofPart::Gable).size() == 2);
}

TEST_CASE("an A-frame roof that is not steep or not convex is built as a gable", "[roofs][more]")
{
    Plan plan = rectanglePlan();
    roofWith(plan, "a_frame", 0.3, 30.0);
    checkFallsBack(plan, "gable");

    for (Plan shape : {lPlan(), tPlan(), uPlan()}) {
        roofWith(shape, "a_frame", 0.3, 60.0);
        checkFallsBack(shape, "gable");
    }
}
