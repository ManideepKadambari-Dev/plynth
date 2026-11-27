#include "RoofShapeChecks.h"

#include <plynth/core/Roofs.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using namespace plynth::test::roofshape;
using Catch::Approx;

namespace {

const std::vector<double> kOneLevel{0.0};

// A main block 4 m deep from x 0 to length, and wings from x0 to x1 that start 0.1 m past its side at plan y 4
// and reach 5 m on, all under a gable default of 30 degrees with a 0.3 m overhang.
Plan winged(double length, const std::vector<std::pair<double, double>>& wings)
{
    Plan plan = makeNewProject().plan;
    addBoxRoom(plan.floors[0], 1, 0.0, 0.0, length, 4.0);
    int n = 2;
    for (const auto& [x0, x1] : wings) addBoxRoom(plan.floors[0], n++, x0, 4.1, x1, 9.1);
    plan.roof.type = "gable";
    plan.roof.pitch = 30.0;
    plan.roof.overhang = 0.3;
    return plan;
}

// The covering faces of one roof.
std::vector<RoofFace> coveringOfRoof(const RoofBuild& build)
{
    return facesOf(std::vector<RoofBuild>{build}, RoofPart::Covering);
}

// The extent of faces seen from above.
struct Extent {
    double x0 = 1e18;
    double x1 = -1e18;
    double y0 = 1e18;
    double y1 = -1e18;
};

// Measures the extent of the faces.
Extent extentOf(const std::vector<RoofFace>& faces)
{
    Extent e;
    for (const RoofFace& face : faces) {
        for (const Vec3& p : face.outline) {
            e.x0 = std::min(e.x0, p.x);
            e.x1 = std::max(e.x1, p.x);
            e.y0 = std::min(e.y0, p.y);
            e.y1 = std::max(e.y1, p.y);
        }
    }
    return e;
}

// The roof whose covering reaches out to the far end of the wing over x, which is at world y -9.4.
const RoofBuild& wingAt(const FloorRoofs& roofs, double x)
{
    for (const RoofBuild& build : roofs.roofs) {
        const Extent e = extentOf(coveringOfRoof(build));
        if (e.y0 < -9.0 && e.x0 < x && e.x1 > x) return build;
    }
    FAIL("no wing over x " << x);
    return roofs.roofs.front();
}

// The roof whose covering reaches the main block's far eaves at world y 0.3.
const RoofBuild& mainOf(const FloorRoofs& roofs)
{
    for (const RoofBuild& build : roofs.roofs) {
        if (extentOf(coveringOfRoof(build)).y1 > 0.2) return build;
    }
    FAIL("no main roof");
    return roofs.roofs.front();
}

// How the coverings of different roofs meet: corners of one that lie on an edge of another, and how many of
// those stand at another height there (a gap in the valley).
struct Meeting {
    int shared = 0;
    int apart = 0;
};

// Finds where the coverings of different roofs meet.
Meeting meetingOf(const std::vector<RoofBuild>& builds)
{
    Meeting out;
    for (std::size_t i = 0; i < builds.size(); ++i) {
        for (std::size_t j = 0; j < builds.size(); ++j) {
            if (i == j) continue;
            for (const RoofFace& corner : coveringOfRoof(builds[j])) {
                for (const Vec3& v : corner.outline) {
                    for (const RoofFace& face : coveringOfRoof(builds[i])) {
                        const std::vector<Vec3>& ring = face.outline;
                        for (std::size_t k = 0; k < ring.size(); ++k) {
                            const Vec3& a = ring[k];
                            const Vec3& b = ring[(k + 1) % ring.size()];
                            const SegmentProjection on = projectOntoSegment({v.x, v.y}, {a.x, a.y}, {b.x, b.y});
                            if (on.distance > 1e-6) continue;
                            const double len = std::hypot(b.x - a.x, b.y - a.y);
                            const double t = len > 0.0 ? on.offset / len : 0.0;
                            ++out.shared;
                            if (std::fabs(a.z + (b.z - a.z) * t - v.z) > 1e-6) ++out.apart;
                        }
                    }
                }
            }
        }
    }
    return out;
}

// Checks a joined floor: closed planar faces, the covering adding up to the eaves, edges only on the outline,
// closed convex hulls, and valleys with no gap in height.
void checkJoined(const Plan& plan, const FloorRoofs& r)
{
    CHECK(r.problems.empty());
    checkFaces(r.roofs);
    checkCoversEaves(plan, 0, r.roofs, 0.3);
    checkEdgesOnEaves(plan, 0, r.roofs, 0.3);
    checkHulls(plan, 0, r.roofs);
    const Meeting meeting = meetingOf(r.roofs);
    CHECK(meeting.shared > 0);
    CHECK(meeting.apart == 0);
}

}

TEST_CASE("an L of two gable sections meets in valleys that run up to the main ridge", "[roofs][valleys]")
{
    const Plan plan = winged(8.0, {{0.0, 4.0}});
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 2);
    checkJoined(plan, r);

    const double ridge = 3.2 + 2.0 * kTan30;
    const std::vector<RoofFace> wing = coveringOfRoof(wingAt(r, 2.0));
    CHECK(topOf(wing) == Approx(ridge).margin(1e-6));
    // The wing's ridge runs on over the main roof to the main ridge, where both valleys meet.
    CHECK(extentOf(wing).y1 == Approx(-2.0).margin(1e-6));
    CHECK(hasCorner(wing, 2.0, -2.0, ridge));
    CHECK(topOf(coveringOfRoof(mainOf(r))) == Approx(ridge).margin(1e-6));
}

TEST_CASE("a T of two gable sections meets in valleys at the middle of the main ridge", "[roofs][valleys]")
{
    const Plan plan = winged(12.0, {{4.0, 8.0}});
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 2);
    checkJoined(plan, r);

    const double ridge = 3.2 + 2.0 * kTan30;
    const std::vector<RoofFace> wing = coveringOfRoof(wingAt(r, 6.0));
    CHECK(extentOf(wing).y1 == Approx(-2.0).margin(1e-6));
    CHECK(hasCorner(wing, 6.0, -2.0, ridge));
}

TEST_CASE("a U of three gable sections meets in two pairs of valleys", "[roofs][valleys]")
{
    const Plan plan = winged(12.0, {{0.0, 4.0}, {8.0, 12.0}});
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 3);
    checkJoined(plan, r);

    const double ridge = 3.2 + 2.0 * kTan30;
    CHECK(hasCorner(coveringOfRoof(wingAt(r, 2.0)), 2.0, -2.0, ridge));
    CHECK(hasCorner(coveringOfRoof(wingAt(r, 10.0)), 10.0, -2.0, ridge));
    CHECK(topOf(coveringOfRoof(mainOf(r))) == Approx(ridge).margin(1e-6));
}

TEST_CASE("a narrower wing runs into the main roof below its ridge", "[roofs][valleys]")
{
    const Plan plan = winged(12.0, {{4.5, 7.5}});
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 2);
    checkJoined(plan, r);

    // The wing is 3 m wide, so its ridge stands 1.5 m of run above base and meets the main roof 2.5 m in.
    const double low = 3.2 + 1.5 * kTan30;
    const std::vector<RoofFace> wing = coveringOfRoof(wingAt(r, 6.0));
    CHECK(topOf(wing) == Approx(low).margin(1e-6));
    CHECK(extentOf(wing).y1 == Approx(-2.5).margin(1e-6));
    CHECK(hasCorner(wing, 6.0, -2.5, low));
    CHECK(topOf(coveringOfRoof(mainOf(r))) == Approx(3.2 + 2.0 * kTan30).margin(1e-6));
}

TEST_CASE("two gable sections side by side meet in a valley between them", "[roofs][valleys]")
{
    Plan plan = makeNewProject().plan;
    addBoxRoom(plan.floors[0], 1, 0.0, 0.0, 8.0, 4.0);
    addBoxRoom(plan.floors[0], 2, 0.0, 4.1, 8.0, 8.1);
    plan.roof.type = "gable";
    plan.roof.pitch = 30.0;
    plan.roof.overhang = 0.3;
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 2);
    checkJoined(plan, r);

    // Both ridges run along x; the slopes falling toward each other meet halfway across the gap.
    const std::vector<RoofFace> coverings = facesOf(r.roofs, RoofPart::Covering);
    const double valley = 3.2 - 0.05 * kTan30;
    CHECK(hasCorner(coverings, -0.3, -4.05, valley));
    CHECK(hasCorner(coverings, 8.3, -4.05, valley));
    for (const RoofBuild& build : r.roofs) CHECK(topOf(coveringOfRoof(build)) == Approx(3.2 + 2.0 * kTan30).margin(1e-6));
}

TEST_CASE("sections whose eaves do not overlap are built as they are alone", "[roofs][valleys]")
{
    Plan apart = makeNewProject().plan;
    addBoxRoom(apart.floors[0], 1, 0.0, 0.0, 8.0, 4.0);
    addBoxRoom(apart.floors[0], 2, 0.0, 5.0, 4.0, 10.0);
    apart.roof.type = "gable";
    apart.roof.overhang = 0.3;
    Plan alone = makeNewProject().plan;
    addBoxRoom(alone.floors[0], 2, 0.0, 5.0, 4.0, 10.0);
    alone.roof = apart.roof;

    const FloorRoofs both = floorRoofs(apart, 0, kOneLevel);
    const FloorRoofs one = floorRoofs(alone, 0, kOneLevel);
    REQUIRE(both.roofs.size() == 2);
    REQUIRE(one.roofs.size() == 1);
    bool found = false;
    for (const RoofBuild& build : both.roofs) {
        if (extentOf(coveringOfRoof(build)).y0 > -9.0) continue;
        found = true;
        for (const RoofPart part : {RoofPart::Covering, RoofPart::Underside, RoofPart::Edge, RoofPart::Gable}) {
            CHECK(facesOf({build}, part).size() == facesOf(one.roofs, part).size());
        }
        CHECK(coveringOf({build}).sum == Approx(coveringOf(one.roofs).sum).margin(1e-9));
        CHECK(topOf(coveringOfRoof(build)) == Approx(topOf(coveringOfRoof(one.roofs[0]))).margin(1e-9));
    }
    CHECK(found);
}

TEST_CASE("a flat section beside a gable section is not joined", "[roofs][valleys]")
{
    Plan plan = winged(8.0, {{0.0, 4.0}});
    RoofSection flat;
    flat.id = "rs1";
    flat.floor = plan.floors[0].id;
    flat.x = 2.0;
    flat.y = 6.0;
    flat.settings.type = "flat";
    flat.settings.parapet = 0.0;
    flat.settings.overhang = 0.3;
    plan.roofSections.push_back(flat);
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 2);
    checkFaces(r.roofs);
    // The gable keeps its whole covering, as it does with no neighbour.
    const Covering main = coveringOf({mainOf(r)});
    CHECK(main.sum == Approx(8.6 * 4.6).margin(1e-4));
}
