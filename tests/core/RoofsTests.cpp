#include "RoomFixtures.h"

#include <plynth/core/Roofs.h>
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

const double kTan30 = std::tan(30.0 * std::numbers::pi / 180.0);

// Adds a room made of the four walls of rectangle n.
void addRoomOf(Floor& f, int n, const std::string& id)
{
    const std::string s = std::to_string(n);
    Room r;
    r.id = id;
    r.walls = {"w" + s + "1", "w" + s + "2", "w" + s + "3", "w" + s + "4"};
    f.rooms.push_back(r);
}

// One floor 6 by 4 m outside (walls 0.2, ceiling 3, slab 0.2), so the roof base is 3.2.
Plan oneFloor()
{
    Plan plan = makeNewProject().plan;
    addRectangle(plan.floors[0], 1, 0.1, 0.1, 5.9, 3.9, 0.2);
    addRoomOf(plan.floors[0], 1, "r1");
    return plan;
}

// The same with a second floor over the left half (x 0 to 3 outside).
Plan twoFloors()
{
    Plan plan = oneFloor();
    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    addRectangle(upper, 1, 0.1, 0.1, 2.9, 3.9, 0.2);
    addRoomOf(upper, 1, "r1");
    plan.floors.push_back(upper);
    return plan;
}

const std::vector<double> kOneLevel{0.0};
const std::vector<double> kTwoLevels{0.0, 3.2};

// The faces of a part.
std::vector<RoofFace> facesOf(const RoofBuild& build, RoofPart part)
{
    std::vector<RoofFace> out;
    for (const RoofFace& f : build.faces) {
        if (f.part == part) out.push_back(f);
    }
    return out;
}

// The area of a face seen from above (its outline minus its holes).
double faceArea(const RoofFace& f)
{
    auto ring = [](const std::vector<Vec3>& pts) {
        double s = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const Vec3& a = pts[i];
            const Vec3& b = pts[(i + 1) % pts.size()];
            s += a.x * b.y - b.x * a.y;
        }
        return s / 2.0;
    };
    double total = std::fabs(ring(f.outline));
    for (const auto& h : f.holes) total -= std::fabs(ring(h));
    return total;
}

// The extent of the faces.
struct Box {
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9, z0 = 1e9, z1 = -1e9;
};

Box boxOf(const std::vector<RoofFace>& faces)
{
    Box b;
    for (const RoofFace& f : faces) {
        for (const Vec3& p : f.outline) {
            b.x0 = std::min(b.x0, p.x);
            b.x1 = std::max(b.x1, p.x);
            b.y0 = std::min(b.y0, p.y);
            b.y1 = std::max(b.y1, p.y);
            b.z0 = std::min(b.z0, p.z);
            b.z1 = std::max(b.z1, p.z);
        }
    }
    return b;
}

// The normal of an outline by Newell's method, not scaled.
Vec3 newell(const std::vector<Vec3>& pts)
{
    Vec3 n;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Vec3& a = pts[i];
        const Vec3& b = pts[(i + 1) % pts.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
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

// Checks that every face is planar and looks the way its part says: coverings up, undersides down.
void checkFaces(const RoofBuild& build)
{
    for (const RoofFace& f : build.faces) {
        REQUIRE(f.outline.size() >= 3);
        const Vec3 n = newell(f.outline);
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        REQUIRE(len > 1e-9);
        std::vector<std::vector<Vec3>> rings{f.outline};
        for (const auto& h : f.holes) rings.push_back(h);
        for (const auto& ring : rings) {
            for (const Vec3& p : ring) {
                const double d =
                    ((p.x - f.outline[0].x) * n.x + (p.y - f.outline[0].y) * n.y + (p.z - f.outline[0].z) * n.z) / len;
                CHECK(std::fabs(d) < 1e-6);
            }
        }
        if (f.part == RoofPart::Covering) CHECK(n.z / len > 0.5);
        if (f.part == RoofPart::Underside) CHECK(n.z / len < -0.5);
        if (f.part == RoofPart::Gable || f.part == RoofPart::Edge) CHECK(std::fabs(n.z / len) < 1e-6);
    }
}

}

TEST_CASE("a single floor has one roof area of 24 square metres", "[roofs]")
{
    const Plan plan = oneFloor();
    const std::vector<RoofArea> areas = roofAreas(plan);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].floor == "f1");
    CHECK(areas[0].index == 0);
    CHECK(planAreaSize(areas[0].area) == Approx(24.0).margin(1e-6));
    CHECK(areas[0].section.empty());
    CHECK(areas[0].settings == plan.roof);
    CHECK(areas[0].labelPoint.x == Approx(3.0).margin(0.3));
    CHECK(areas[0].labelPoint.y == Approx(2.0).margin(0.3));
    CHECK(roofAreasOf(plan, "f1").size() == 1);
    CHECK(roofAreasOf(plan, "nothing").empty());
}

TEST_CASE("the default flat roof has a layer and a parapet", "[roofs]")
{
    const Plan plan = oneFloor();
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.problems.empty());
    const RoofBuild& b = r.roofs[0];
    CHECK(b.type == "flat");
    checkFaces(b);

    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 1);
    CHECK(faceArea(covering[0]) == Approx(24.0).margin(1e-6));
    for (const Vec3& p : covering[0].outline) CHECK(p.z == Approx(3.35).margin(1e-9));
    const std::vector<RoofFace> under = facesOf(b, RoofPart::Underside);
    REQUIRE(under.size() == 1);
    for (const Vec3& p : under[0].outline) CHECK(p.z == Approx(3.2).margin(1e-9));

    const std::vector<RoofFace> parapet = facesOf(b, RoofPart::Parapet);
    REQUIRE_FALSE(parapet.empty());
    const Box pb = boxOf(parapet);
    CHECK(pb.z0 == Approx(3.35).margin(1e-9));
    CHECK(pb.z1 == Approx(3.95).margin(1e-9));
    double topArea = 0.0;
    for (const RoofFace& f : parapet) {
        if (boxOf({f}).z0 > 3.94) topArea += faceArea(f);
    }
    CHECK(topArea == Approx(24.0 - 5.6 * 3.6).margin(1e-6));
    CHECK(pb.x0 == Approx(0.0).margin(1e-9));
    CHECK(pb.x1 == Approx(6.0).margin(1e-9));
    CHECK(pb.y0 == Approx(-4.0).margin(1e-9));
    CHECK(pb.y1 == Approx(0.0).margin(1e-9));
}

TEST_CASE("a flat roof without a parapet reaches out by the overhang", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.parapet = 0.0;
    plan.roof.overhang = 0.3;
    const RoofBuild b = floorRoofs(plan, 0, kOneLevel).roofs[0];
    checkFaces(b);
    CHECK(facesOf(b, RoofPart::Parapet).empty());
    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 1);
    CHECK(faceArea(covering[0]) == Approx(6.6 * 4.6).margin(1e-6));
    const Box box = boxOf(covering);
    CHECK(box.x0 == Approx(-0.3).margin(1e-9));
    CHECK(box.x1 == Approx(6.3).margin(1e-9));
    CHECK(box.z0 == Approx(3.35).margin(1e-9));
    CHECK(facesOf(b, RoofPart::Edge).size() == 4);
}

TEST_CASE("a gable roof rises to a ridge along the longer side", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "gable";
    plan.roof.pitch = 30.0;
    plan.roof.overhang = 0.3;
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.problems.empty());
    const RoofBuild& b = r.roofs[0];
    CHECK(b.type == "gable");
    checkFaces(b);

    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 2);
    for (const RoofFace& f : covering) {
        for (const Vec3& p : f.outline) {
            const double expected = 3.2 + (2.0 - std::fabs(p.y + 2.0)) * kTan30;
            CHECK(p.z == Approx(expected).margin(1e-6));
        }
    }
    const Box box = boxOf(covering);
    CHECK(box.z1 == Approx(3.2 + 2.0 * kTan30).margin(1e-6));
    CHECK(box.z0 == Approx(3.2 - 0.3 * kTan30).margin(1e-6));
    CHECK(box.x0 == Approx(-0.3).margin(1e-9));
    CHECK(box.x1 == Approx(6.3).margin(1e-9));
    CHECK(box.y0 == Approx(-4.3).margin(1e-9));
    CHECK(box.y1 == Approx(0.3).margin(1e-9));
    for (const RoofFace& f : covering) {
        double top = -1e9;
        for (const Vec3& p : f.outline) top = std::max(top, p.z);
        CHECK(top == Approx(3.2 + 2.0 * kTan30).margin(1e-6));
    }

    const std::vector<RoofFace> gables = facesOf(b, RoofPart::Gable);
    REQUIRE(gables.size() == 2);
    std::vector<double> xs;
    for (const RoofFace& f : gables) {
        const Box fb = boxOf({f});
        CHECK(fb.x0 == Approx(fb.x1).margin(1e-9));
        CHECK(fb.z0 == Approx(3.2).margin(1e-9));
        CHECK(fb.z1 == Approx(3.2 + 2.0 * kTan30 - kRoofThickness).margin(1e-6));
        const Vec3 n = newell(f.outline);
        CHECK((fb.x0 < 3.0 ? -n.x : n.x) > 0.0);
        xs.push_back(fb.x0);
    }
    std::sort(xs.begin(), xs.end());
    CHECK(xs[0] == Approx(0.0).margin(1e-9));
    CHECK(xs[1] == Approx(6.0).margin(1e-9));
}

TEST_CASE("a gable with a set direction runs its ridge that way", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "gable";
    plan.roof.direction = 90.0;
    const RoofBuild b = floorRoofs(plan, 0, kOneLevel).roofs[0];
    checkFaces(b);
    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 2);
    for (const RoofFace& f : covering) {
        for (const Vec3& p : f.outline) {
            const double expected = 3.2 + (3.0 - std::fabs(p.x - 3.0)) * kTan30;
            CHECK(p.z == Approx(expected).margin(1e-6));
        }
    }
}

TEST_CASE("a shed roof falls toward plus y on the plan", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "shed";
    plan.roof.pitch = 30.0;
    plan.roof.overhang = 0.3;
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    CHECK(r.problems.empty());
    const RoofBuild& b = r.roofs[0];
    CHECK(b.type == "shed");
    checkFaces(b);
    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 1);
    for (const Vec3& p : covering[0].outline) {
        const double expected = 3.2 + (p.y + 4.0) * kTan30;
        CHECK(p.z == Approx(expected).margin(1e-6));
    }
    CHECK(boxOf(covering).z0 == Approx(3.2 - 0.3 * kTan30).margin(1e-6));
    const std::vector<RoofFace> gables = facesOf(b, RoofPart::Gable);
    REQUIRE_FALSE(gables.empty());
    for (const RoofFace& f : gables) CHECK(boxOf({f}).z0 == Approx(3.2).margin(1e-9));
}

TEST_CASE("a shed with a set direction falls that way", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "shed";
    plan.roof.direction = 0.0;
    const RoofBuild b = floorRoofs(plan, 0, kOneLevel).roofs[0];
    checkFaces(b);
    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 1);
    for (const Vec3& p : covering[0].outline) CHECK(p.z == Approx(3.2 + (6.0 - p.x) * kTan30).margin(1e-6));
}

TEST_CASE("an upper floor over half leaves the other half as the roof section", "[roofs]")
{
    Plan plan = twoFloors();
    plan.roof.type = "gable";
    plan.roof.overhang = 0.3;
    const std::vector<RoofArea> areas = roofAreas(plan);
    REQUIRE(areas.size() == 2);
    CHECK(areas[0].floor == "f1");
    CHECK(areas[1].floor == "f2");
    CHECK(planAreaSize(areas[0].area) == Approx(12.0).margin(1e-6));
    CHECK(planAreaSize(areas[1].area) == Approx(12.0).margin(1e-6));
    CHECK(areas[0].labelPoint.x > 3.0);

    const FloorRoofs low = floorRoofs(plan, 0, kTwoLevels);
    REQUIRE(low.roofs.size() == 1);
    const RoofBuild& b = low.roofs[0];
    checkFaces(b);
    const Box box = boxOf(facesOf(b, RoofPart::Covering));
    // The eaves wrap round the corner of the upper floor but never enter its footprint (x 0 to 3, world y -4 to 0).
    for (const RoofFace& f : facesOf(b, RoofPart::Covering)) {
        for (const Vec3& p : f.outline) {
            if (p.y < -1e-9 && p.y > -4.0 + 1e-9) CHECK(p.x >= 3.0 - 1e-9);
        }
    }
    CHECK(box.x0 == Approx(2.7).margin(1e-6));
    CHECK(box.x1 == Approx(6.3).margin(1e-9));
    // The section is 3 wide across x, so the ridge runs along y at x 4.5.
    CHECK(box.z1 == Approx(3.2 + 1.5 * kTan30).margin(1e-6));

    const FloorRoofs top = floorRoofs(plan, 1, kTwoLevels);
    REQUIRE(top.roofs.size() == 1);
    CHECK(boxOf(facesOf(top.roofs[0], RoofPart::Covering)).z0 > 6.4 - 0.3 * kTan30 - 1e-6);
}

namespace {

// The faces whose every point has x at value, or y at value when alongY.
std::vector<RoofFace> facesOnLine(const std::vector<RoofFace>& faces, double value, bool alongY)
{
    std::vector<RoofFace> out;
    for (const RoofFace& f : faces) {
        bool on = !f.outline.empty();
        for (const Vec3& p : f.outline) on = on && std::fabs((alongY ? p.y : p.x) - value) < 1e-6;
        if (on) out.push_back(f);
    }
    return out;
}

// The area seen from above of the parapet faces that lie at height z.
double parapetTopArea(const RoofBuild& build, double z)
{
    double area = 0.0;
    for (const RoofFace& f : facesOf(build, RoofPart::Parapet)) {
        if (boxOf({f}).z0 > z - 1e-9) area += faceArea(f);
    }
    return area;
}

}

TEST_CASE("a single storey flat roof has a parapet and an edge on every side", "[roofs]")
{
    const RoofBuild b = floorRoofs(oneFloor(), 0, kOneLevel).roofs[0];
    const std::vector<RoofFace> edges = facesOf(b, RoofPart::Edge);
    const std::vector<RoofFace> parapet = facesOf(b, RoofPart::Parapet);
    CHECK(edges.size() == 4);
    for (const double x : {0.0, 6.0}) {
        CHECK(facesOnLine(edges, x, false).size() == 1);
        CHECK(facesOnLine(parapet, x, false).size() == 1);
    }
    for (const double y : {0.0, -4.0}) {
        CHECK(facesOnLine(edges, y, true).size() == 1);
        CHECK(facesOnLine(parapet, y, true).size() == 1);
    }
}

TEST_CASE("a flat roof meets the wall of the floor above with no parapet and no edge", "[roofs]")
{
    const Plan plan = twoFloors();
    const FloorRoofs low = floorRoofs(plan, 0, kTwoLevels);
    REQUIRE(low.roofs.size() == 1);
    const RoofBuild& b = low.roofs[0];
    checkFaces(b);

    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 1);
    CHECK(faceArea(covering[0]) == Approx(12.0).margin(1e-6));

    // The upper floor's wall stands along x 3, so nothing of the roof's edge or parapet lies there.
    const std::vector<RoofFace> edges = facesOf(b, RoofPart::Edge);
    const std::vector<RoofFace> parapet = facesOf(b, RoofPart::Parapet);
    CHECK(facesOnLine(edges, 3.0, false).empty());
    CHECK(facesOnLine(parapet, 3.0, false).empty());
    CHECK(edges.size() == 3);

    // The three free sides keep their parapet, which runs right up to that wall.
    CHECK(facesOnLine(parapet, 6.0, false).size() == 1);
    CHECK(facesOnLine(parapet, 0.0, true).size() == 1);
    CHECK(facesOnLine(parapet, -4.0, true).size() == 1);
    CHECK(facesOnLine(edges, 6.0, false).size() == 1);
    CHECK(facesOnLine(edges, 0.0, true).size() == 1);
    CHECK(facesOnLine(edges, -4.0, true).size() == 1);
    const Box pb = boxOf(parapet);
    CHECK(pb.x0 == Approx(3.0).margin(1e-6));
    CHECK(pb.x1 == Approx(6.0).margin(1e-6));
    CHECK(pb.z1 == Approx(3.95).margin(1e-9));
    CHECK(parapetTopArea(b, 3.95) == Approx(12.0 - 2.8 * 3.6).margin(1e-6));
    for (const RoofFace& f : parapet) {
        for (const Vec3& p : f.outline) CHECK(p.x >= 3.0 - 1e-6);
    }

    // The upper floor has nothing above it and keeps its parapet all round.
    const RoofBuild& top = floorRoofs(plan, 1, kTwoLevels).roofs[0];
    CHECK(facesOf(top, RoofPart::Edge).size() == 4);
    CHECK(facesOnLine(facesOf(top, RoofPart::Parapet), 3.0, false).size() == 1);
    CHECK(parapetTopArea(top, boxOf(facesOf(top, RoofPart::Parapet)).z1) == Approx(12.0 - 2.6 * 3.6).margin(1e-6));
}

TEST_CASE("a flat roof with an overhang has no edge along the floor above", "[roofs]")
{
    Plan plan = twoFloors();
    plan.roof.parapet = 0.0;
    plan.roof.overhang = 0.3;
    const RoofBuild b = floorRoofs(plan, 0, kTwoLevels).roofs[0];
    checkFaces(b);
    CHECK(facesOf(b, RoofPart::Parapet).empty());
    // The eaves wrap round the upper floor's corners, so of their eight sides three lie along its walls.
    const std::vector<RoofFace> edges = facesOf(b, RoofPart::Edge);
    CHECK(edges.size() == 5);
    CHECK(facesOnLine(edges, 3.0, false).empty());
    for (const RoofFace& f : edges) {
        const Box box = boxOf({f});
        const bool alongTop = std::fabs(box.y0) < 1e-6 && std::fabs(box.y1) < 1e-6;
        const bool alongBottom = std::fabs(box.y0 + 4.0) < 1e-6 && std::fabs(box.y1 + 4.0) < 1e-6;
        CHECK_FALSE(alongTop);
        CHECK_FALSE(alongBottom);
    }
}

TEST_CASE("a pitched roof has no edge along the floor above", "[roofs]")
{
    for (const char* type : {"gable", "shed", "hip", "sawtooth"}) {
        Plan plan = twoFloors();
        plan.roof.type = type;
        plan.roof.pitch = 30.0;
        plan.roof.overhang = 0.0;
        const RoofBuild b = floorRoofs(plan, 0, kTwoLevels).roofs[0];
        INFO(type);
        checkFaces(b);
        CHECK(facesOnLine(facesOf(b, RoofPart::Edge), 3.0, false).empty());
        CHECK_FALSE(facesOnLine(facesOf(b, RoofPart::Edge), 0.0, true).empty());
    }
}

TEST_CASE("an entry governs the section its anchor lies in", "[roofs]")
{
    Plan plan = twoFloors();
    RoofSection first;
    first.id = "rf1";
    first.floor = "f1";
    first.x = 4.5;
    first.y = 2.0;
    first.settings.type = "shed";
    first.material = "roof_tiles";
    RoofSection second = first;
    second.id = "rf2";
    second.x = 5.0;
    second.y = 1.0;
    second.settings.type = "gable";
    RoofSection outside = first;
    outside.id = "rf3";
    outside.x = 1.0;
    outside.y = 1.0;
    plan.roofSections = {first, second, outside};

    const std::vector<RoofArea> areas = roofAreasOf(plan, "f1");
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].section == "rf1");
    CHECK(areas[0].settings.type == "shed");
    CHECK(areas[0].material == std::optional<std::string>("roof_tiles"));
    CHECK(roofAreasOf(plan, "f2")[0].section.empty());

    const std::vector<Id> unused = unusedRoofSections(plan);
    REQUIRE(unused.size() == 2);
    CHECK(unused[0] == "rf2");
    CHECK(unused[1] == "rf3");

    const std::optional<RoofArea> at = roofAreaAt(plan, "f1", {4.5, 2.0});
    REQUIRE(at.has_value());
    CHECK(at->section == "rf1");
    CHECK_FALSE(roofAreaAt(plan, "f1", {1.0, 1.0}).has_value());
    CHECK_FALSE(roofAreaAt(plan, "nothing", {4.5, 2.0}).has_value());

    const FloorRoofs low = floorRoofs(plan, 0, kTwoLevels);
    CHECK(low.roofs[0].section == "rf1");
    CHECK(low.roofs[0].type == "shed");
    REQUIRE(low.problems.size() == 2);
    CHECK(low.problems[0].code == "roof_section_unused");
    CHECK(low.problems[0].object == "rf2");
    CHECK(low.problems[1].object == "rf3");
    CHECK(floorRoofs(plan, 1, kTwoLevels).problems.empty());
}

TEST_CASE("an entry on a floor that is not there is unused", "[roofs]")
{
    Plan plan = oneFloor();
    RoofSection e;
    e.id = "rf1";
    e.floor = "f9";
    plan.roofSections = {e};
    CHECK(unusedRoofSections(plan) == std::vector<Id>{"rf1"});
}

TEST_CASE("an unknown roof type is built flat and reported", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "dome";
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == "flat");
    REQUIRE(r.problems.size() == 1);
    CHECK(r.problems[0].code == "roof_type_unknown");
    CHECK(r.problems[0].floor == "f1");
}

TEST_CASE("a floor with no walls has no roof", "[roofs]")
{
    const Plan plan = makeNewProject().plan;
    CHECK(roofAreas(plan).empty());
    CHECK(floorRoofs(plan, 0, kOneLevel).roofs.empty());
    CHECK(floorRoofs(plan, 4, kOneLevel).roofs.empty());
}

TEST_CASE("a roof over a courtyard keeps the hole", "[roofs]")
{
    Plan plan = makeNewProject().plan;
    Floor& f = plan.floors[0];
    addRectangle(f, 1, 0.1, 0.1, 7.9, 7.9, 0.2);
    plan.roof.parapet = 0.0;
    plan.roof.overhang = 0.0;
    const std::vector<RoofArea> areas = roofAreas(plan);
    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].area.holes.size() == 1);
    CHECK(insidePolygon(areas[0].labelPoint, areas[0].area.outline));
    CHECK_FALSE(insidePolygon(areas[0].labelPoint, areas[0].area.holes[0]));
    const RoofBuild b = floorRoofs(plan, 0, kOneLevel).roofs[0];
    checkFaces(b);
    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 1);
    CHECK(covering[0].holes.size() == 1);
}

TEST_CASE("the built in roof types are registered in order", "[roofs]")
{
    const std::vector<const RoofType*> types = allRoofTypes();
    std::vector<std::string> ids;
    for (const RoofType* t : types) ids.emplace_back(t->id());
    const auto at = [&](const char* id) { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
    CHECK(at("flat") < at("gable"));
    CHECK(at("gable") < at("shed"));
    CHECK(findRoofType("flat") != nullptr);
    CHECK(findRoofType("nothing") == nullptr);
    CHECK_FALSE(findRoofType("gable")->usesParapet());
    CHECK(findRoofType("flat")->usesParapet());
    CHECK(findRoofType("shed")->usesDirection());
    CHECK_FALSE(findRoofType("flat")->usesPitch());
}

TEST_CASE("a taken or badly formed roof type id is refused", "[roofs]")
{
    class Fake final : public RoofType {
    public:
        explicit Fake(std::string id) : m_id(std::move(id)) {}
        std::string_view id() const override { return m_id; }
        std::string_view name() const override { return "Fake"; }
        int sortKey() const override { return 1; }
        bool usesPitch() const override { return false; }
        bool usesDirection() const override { return false; }
        bool usesOverhang() const override { return false; }
        bool usesParapet() const override { return false; }
        Status fits(const RoofInput&) const override { return {}; }
        std::string_view fallback() const override { return "flat"; }
        std::vector<RoofFace> build(const RoofInput&) const override { return {}; }

    private:
        std::string m_id;
    };
    CHECK_FALSE(registerRoofType(std::make_unique<Fake>("flat")));
    CHECK_FALSE(registerRoofType(std::make_unique<Fake>("Bad Id")));
    CHECK_FALSE(registerRoofType(nullptr));
}

TEST_CASE("roof settings are checked", "[roofs]")
{
    RoofSettings s;
    CHECK(checkRoofSettings(s).ok());
    s.type = "gable";
    CHECK(checkRoofSettings(s).ok());
    s.pitch = 2.0;
    CHECK(checkRoofSettings(s).code == ErrorCode::InvalidValue);
    s.type = "flat";
    CHECK(checkRoofSettings(s).ok());
    s.pitch = 80.0;
    CHECK(checkRoofSettings(s).code == ErrorCode::InvalidValue);
    s = RoofSettings{};
    s.type = "dome";
    CHECK(checkRoofSettings(s).code == ErrorCode::InvalidValue);
    s = RoofSettings{};
    s.overhang = 2.5;
    CHECK(checkRoofSettings(s).code == ErrorCode::InvalidValue);
    s = RoofSettings{};
    s.parapet = -0.1;
    CHECK(checkRoofSettings(s).code == ErrorCode::InvalidValue);
    s = RoofSettings{};
    s.direction = 360.0;
    CHECK(checkRoofSettings(s).code == ErrorCode::InvalidValue);
    s.direction = 359.0;
    CHECK(checkRoofSettings(s).ok());
    s.direction = std::nan("");
    CHECK(checkRoofSettings(s).code == ErrorCode::InvalidValue);
}

TEST_CASE("a hip roof on a rectangle has two trapezoids and two triangles", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "hip";
    plan.roof.pitch = 30.0;
    plan.roof.overhang = 0.0;
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    CHECK(r.problems.empty());
    const RoofBuild& b = r.roofs[0];
    CHECK(b.type == "hip");
    checkFaces(b);
    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 4);
    int trapezoids = 0;
    int triangles = 0;
    for (const RoofFace& f : covering) {
        const int corners = cornerCount(f);
        if (corners == 4) ++trapezoids;
        if (corners == 3) ++triangles;
    }
    CHECK(trapezoids == 2);
    CHECK(triangles == 2);
    const double ridge = 3.2 + 2.0 * kTan30;
    CHECK(boxOf(covering).z1 == Approx(ridge).margin(1e-6));
    double lowX = 1e9;
    double highX = -1e9;
    for (const RoofFace& f : covering) {
        for (const Vec3& p : f.outline) {
            if (std::fabs(p.z - ridge) < 1e-6) {
                lowX = std::min(lowX, p.x);
                highX = std::max(highX, p.x);
                CHECK(p.y == Approx(-2.0).margin(1e-6));
            }
        }
    }
    CHECK(highX - lowX == Approx(2.0).margin(1e-6));
    CHECK(facesOf(b, RoofPart::Gable).empty());
}

TEST_CASE("a hip roof with an overhang still rises to the same ridge", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "hip";
    plan.roof.overhang = 0.3;
    const RoofBuild b = floorRoofs(plan, 0, kOneLevel).roofs[0];
    checkFaces(b);
    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    CHECK(covering.size() == 4);
    CHECK(boxOf(covering).z1 == Approx(3.2 + 2.0 * kTan30).margin(1e-6));
    CHECK(boxOf(covering).z0 == Approx(3.2 - 0.3 * kTan30).margin(1e-6));
}

TEST_CASE("a hip roof on an L shaped section falls back to a gable", "[roofs]")
{
    Plan plan = makeNewProject().plan;
    Floor& f = plan.floors[0];
    addPoint(f, "p1", 0.1, 0.1);
    addPoint(f, "p2", 5.9, 0.1);
    addPoint(f, "p3", 5.9, 1.9);
    addPoint(f, "p4", 2.9, 1.9);
    addPoint(f, "p5", 2.9, 3.9);
    addPoint(f, "p6", 0.1, 3.9);
    const char* ends[6][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p5"}, {"p5", "p6"}, {"p6", "p1"}};
    Room room;
    room.id = "r1";
    for (int i = 0; i < 6; ++i) {
        const std::string id = "w" + std::to_string(i + 1);
        addWall(f, id, ends[i][0], ends[i][1], 0.2);
        room.walls.push_back(id);
    }
    f.rooms.push_back(room);
    plan.roof.type = "hip";
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == "gable");
    REQUIRE(r.problems.size() == 1);
    CHECK(r.problems[0].code == "roof_type_fallback");
    CHECK_FALSE(r.problems[0].message.empty());
    checkFaces(r.roofs[0]);
}

TEST_CASE("a sawtooth roof has a bay of 3 m and a bay of 1 m with glazing each", "[roofs]")
{
    Plan plan = oneFloor();
    plan.roof.type = "sawtooth";
    plan.roof.pitch = 30.0;
    plan.roof.overhang = 0.3;
    plan.roof.direction = 0.0;
    const FloorRoofs r = floorRoofs(plan, 0, kOneLevel);
    CHECK(r.problems.empty());
    const RoofBuild& b = r.roofs[0];
    CHECK(b.type == "sawtooth");
    checkFaces(b);

    const std::vector<RoofFace> covering = facesOf(b, RoofPart::Covering);
    REQUIRE(covering.size() == 2);
    std::vector<double> tops;
    for (const RoofFace& f : covering) tops.push_back(boxOf({f}).z1);
    std::sort(tops.begin(), tops.end());
    CHECK(tops[0] == Approx(3.2 + 1.0 * kTan30).margin(1e-6));
    CHECK(tops[1] == Approx(3.2 + 3.0 * kTan30).margin(1e-6));

    const std::vector<RoofFace> glazing = facesOf(b, RoofPart::Glazing);
    REQUIRE(glazing.size() == 2);
    std::vector<std::pair<double, double>> at; // world y and top height
    for (const RoofFace& f : glazing) {
        const Box fb = boxOf({f});
        CHECK(fb.y0 == Approx(fb.y1).margin(1e-9));
        CHECK(fb.z0 == Approx(3.2).margin(1e-9));
        CHECK(fb.x0 == Approx(-0.3).margin(1e-6));
        CHECK(fb.x1 == Approx(6.3).margin(1e-6));
        const Vec3 n = newell(f.outline);
        CHECK(n.y > 0.0);
        at.emplace_back(fb.y0, fb.z1);
    }
    std::sort(at.begin(), at.end());
    CHECK(at[0].first == Approx(-1.0).margin(1e-9));
    CHECK(at[0].second == Approx(3.2 + 3.0 * kTan30).margin(1e-6));
    CHECK(at[1].first == Approx(0.0).margin(1e-9));
    CHECK(at[1].second == Approx(3.2 + 1.0 * kTan30).margin(1e-6));
    CHECK_FALSE(facesOf(b, RoofPart::Gable).empty());
}
