#include "DormerFixtures.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildParts.h>
#include <plynth/core/Dormers.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/MaterialSlots.h>
#include <plynth/core/Roofs.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using namespace plynth::test::roofshape;
using Catch::Approx;

namespace {

// The front height of gableDormer and the depth at which its cheeks meet the main roof.
const double kGableFront = 2.2 - 0.8 * kTan30;

// The rings of a face: its outline, then its holes.
std::vector<std::vector<Vec3>> ringsOf(const PartFace& face)
{
    std::vector<std::vector<Vec3>> out{face.outline};
    for (const auto& hole : face.holes) out.push_back(hole);
    return out;
}

// Says whether two points are the same within rounding.
bool same(const Vec3& a, const Vec3& b)
{
    return std::fabs(a.x - b.x) < 1e-7 && std::fabs(a.y - b.y) < 1e-7 && std::fabs(a.z - b.z) < 1e-7;
}

// Checks that the faces close up: every edge is met as often by the same edge the other way round.
void checkClosed(const std::vector<PartFace>& faces)
{
    std::vector<std::pair<Vec3, Vec3>> edges;
    for (const PartFace& face : faces) {
        for (const auto& ring : ringsOf(face)) {
            for (std::size_t i = 0; i < ring.size(); ++i) edges.emplace_back(ring[i], ring[(i + 1) % ring.size()]);
        }
    }
    for (const auto& edge : edges) {
        const Vec3 a = edge.first;
        const Vec3 b = edge.second;
        const auto along = std::count_if(edges.begin(), edges.end(), [&](const auto& e) { return same(e.first, a) && same(e.second, b); });
        const auto back = std::count_if(edges.begin(), edges.end(), [&](const auto& e) { return same(e.first, b) && same(e.second, a); });
        CHECK(along == back);
    }
}

// Checks every face is flat, with at least three points none of which repeats the one before.
void checkPlanar(const std::vector<PartFace>& faces)
{
    for (const PartFace& face : faces) {
        REQUIRE(face.outline.size() >= 3);
        const Vec3 n = newellOf(face.outline);
        const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        REQUIRE(length > 1e-9);
        const Vec3& o = face.outline.front();
        for (const auto& ring : ringsOf(face)) {
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Vec3& p = ring[i];
                CHECK_FALSE(same(p, ring[(i + 1) % ring.size()]));
                CHECK(std::fabs(((p.x - o.x) * n.x + (p.y - o.y) * n.y + (p.z - o.z) * n.z) / length) < 1e-7);
            }
        }
    }
}

// The volume the faces enclose, positive when they all look outward.
double volumeOf(const std::vector<PartFace>& faces)
{
    double total = 0.0;
    for (const PartFace& face : faces) {
        Vec3 n;
        for (const auto& ring : ringsOf(face)) {
            const Vec3 r = newellOf(ring);
            n.x += r.x;
            n.y += r.y;
            n.z += r.z;
        }
        const Vec3& p = face.outline.front();
        total += (n.x * p.x + n.y * p.y + n.z * p.z) / 6.0;
    }
    return total;
}

// The problems of a build plan whose code starts with "dormer_".
std::vector<BuildProblem> dormerProblems(const BuildPlan& plan)
{
    std::vector<BuildProblem> out;
    for (const BuildProblem& p : plan.problems) {
        if (p.code.rfind("dormer_", 0) == 0) out.push_back(p);
    }
    return out;
}

// Builds the project and returns its dormer problems.
std::vector<BuildProblem> problemsOf(const Project& project)
{
    return dormerProblems(makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource()));
}

// The project with one dormer.
Project withDormer(Dormer dormer, Project project = gableHouse())
{
    project.plan.floors[0].dormers.push_back(std::move(dormer));
    return project;
}

// Checks the project's one dormer is left out with the code, and nothing of it is built.
void checkLeftOut(const Project& project, const std::string& code)
{
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    const std::vector<BuildProblem> problems = dormerProblems(plan);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == code);
    CHECK(problems[0].severity == BuildSeverity::Error);
    CHECK(problems[0].floor == "f1");
    CHECK(problems[0].object == project.plan.floors[0].dormers.back().id);
    CHECK(plan.floorParts[0].empty());
    CHECK(plan.roofCuts.empty());
}

// Where the project's last dormer would stand.
DormerPlacement placementOf(const Project& project)
{
    const Plan& plan = project.plan;
    return dormerPlacement(plan, plan.floors[0], plan.floors[0].dormers.back(), floorElevations(plan));
}

// The area of the faces of one part seen from above, holes taken off.
double areaFromAbove(const std::vector<RoofBuild>& roofs, RoofPart part)
{
    double total = 0.0;
    for (const RoofFace& face : facesOf(roofs, part)) total += areaOf(seenFromAbove(face));
    return total;
}

// Says whether a world position lies on a face of one part seen from above.
bool coveredAt(const std::vector<RoofBuild>& roofs, RoofPart part, Vec2 p)
{
    for (const RoofFace& face : facesOf(roofs, part)) {
        const cl::PathsD region = seenFromAbove(face);
        int inside = 0;
        for (const cl::PathD& path : region) {
            if (cl::PointInPolygon(cl::PointD(p.x, p.y), path) == cl::PointInPolygonResult::IsInside) ++inside;
        }
        if (inside % 2 == 1) return true;
    }
    return false;
}

}

TEST_CASE("a gable dormer stands on the slope that holds its front", "[core][dormers]") {
    const Project project = withDormer(gableDormer());
    const DormerPlacement place = placementOf(project);
    REQUIRE(place.status.ok());
    CHECK(place.roofIndex == 0);
    CHECK(place.front.x == Approx(4.0));
    CHECK(place.front.y == Approx(-5.5));
    CHECK(place.facing.x == Approx(0.0).margin(1e-9));
    CHECK(place.facing.y == Approx(-1.0));
    CHECK(place.footZ == Approx(3.7));
    REQUIRE(place.footprint.size() == 5);
    CHECK(signedArea(place.footprint) == Approx(1.6 * kGableFront + 0.8 * (2.2 - kGableFront)));
    const auto backmost = std::max_element(place.footprint.begin(), place.footprint.end(),
                                           [](const Vec2& a, const Vec2& b) { return a.y < b.y; });
    CHECK(backmost->x == Approx(4.0));
    CHECK(backmost->y == Approx(-3.3));
}

TEST_CASE("a gable dormer is one closed part with its window, slots, collision and group", "[core][dormers]") {
    const Project project = withDormer(gableDormer());
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    CHECK(dormerProblems(plan).empty());
    REQUIRE(plan.floorParts[0].size() == 1);
    const BuildPart& part = plan.floorParts[0][0];
    CHECK(part.object == "f1_dm1_Dormer");
    CHECK(part.kind == "dormer");
    CHECK(part.id == "dm1");

    checkPlanar(part.faces);
    checkClosed(part.faces);
    CHECK(volumeOf(part.faces) > 0.5);

    std::set<std::string> faceSlots;
    for (const PartFace& face : part.faces) faceSlots.insert(face.slot);
    CHECK(faceSlots == std::set<std::string>{std::string(kExteriorWallSlot), std::string(kRoofSlot)});
    REQUIRE(part.prisms.size() == 5);
    std::size_t glass = 0;
    for (const PartPrism& prism : part.prisms) {
        CHECK(prism.top > prism.bottom);
        if (prism.slot == kGlassSlot) ++glass;
        else CHECK(prism.slot == kFrameSlot);
    }
    CHECK(glass == 1);
    const auto pane = std::find_if(part.prisms.begin(), part.prisms.end(), [](const PartPrism& p) { return p.slot == kGlassSlot; });
    CHECK(pane->bottom == Approx(3.7 + 0.2 + kOpeningFrameWidth));
    CHECK(pane->top == Approx(3.7 + 1.2 - kOpeningFrameWidth));

    REQUIRE(part.hulls.size() == 3);
    for (const auto& hull : part.hulls) CHECK(hull.size() >= 8);

    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    const auto surfaces = std::find_if(groups.begin(), groups.end(), [](const ExportGroup& g) { return g.id == "surfaces"; });
    REQUIRE(surfaces != groups.end());
    CHECK(std::count(surfaces->members.begin(), surfaces->members.end(), ExportMember{"dormer", Id()}) == 1);
}

TEST_CASE("the main roof is cut where the dormer stands and nowhere else", "[core][dormers]") {
    const Project project = withDormer(gableDormer());
    const std::vector<double> levels = floorElevations(project.plan);
    const std::vector<RoofBuild> uncut = floorRoofs(project.plan, 0, levels).roofs;
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    const std::vector<RoofBuild>& cut = plan.floors[0].roofs;
    const DormerPlacement place = placementOf(project);

    REQUIRE(plan.roofCuts.size() == 1);
    CHECK(plan.roofCuts[0].floor == "f1");
    CHECK(plan.roofCuts[0].index == place.roofIndex);
    REQUIRE(plan.roofCuts[0].cutter.outline.size() == place.footprint.size());
    for (std::size_t i = 0; i < place.footprint.size(); ++i) {
        CHECK(plan.roofCuts[0].cutter.outline[i].x == Approx(place.footprint[i].x));
        CHECK(plan.roofCuts[0].cutter.outline[i].y == Approx(place.footprint[i].y));
    }

    checkFaces(cut);
    const double hole = signedArea(place.footprint);
    CHECK(areaFromAbove(uncut, RoofPart::Covering) - areaFromAbove(cut, RoofPart::Covering) == Approx(hole).epsilon(1e-6));
    CHECK(areaFromAbove(uncut, RoofPart::Underside) - areaFromAbove(cut, RoofPart::Underside) == Approx(hole).epsilon(1e-6));
    CHECK_FALSE(coveredAt(cut, RoofPart::Covering, {4.0, -4.5}));
    CHECK_FALSE(coveredAt(cut, RoofPart::Underside, {4.0, -4.5}));
    CHECK(coveredAt(cut, RoofPart::Covering, {4.0, -5.8}));
    CHECK(coveredAt(cut, RoofPart::Covering, {4.0, -1.0}));
    CHECK(coveredAt(cut, RoofPart::Covering, {2.0, -4.5}));
}

TEST_CASE("a shed dormer on the other slope is closed and cut in too", "[core][dormers]") {
    Project project = withDormer(gableDormer());
    project.plan.floors[0].dormers.push_back(shedDormer());
    const DormerPlacement place = placementOf(project);
    REQUIRE(place.status.ok());
    CHECK(place.facing.y == Approx(1.0));
    REQUIRE(place.footprint.size() == 4);
    CHECK(signedArea(place.footprint) == Approx(1.6 * 2.2));

    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    CHECK(dormerProblems(plan).empty());
    REQUIRE(plan.floorParts[0].size() == 2);
    const BuildPart& shed = plan.floorParts[0][1];
    CHECK(shed.object == "f1_dm2_Dormer");
    checkPlanar(shed.faces);
    checkClosed(shed.faces);
    CHECK(volumeOf(shed.faces) > 0.5);
    CHECK(shed.hulls.size() == 2);
    CHECK(plan.roofCuts.size() == 2);
    CHECK_FALSE(coveredAt(plan.floors[0].roofs, RoofPart::Covering, {4.0, -1.5}));
}

TEST_CASE("a dormer's own materials go to its walls and roof", "[core][dormers]") {
    Dormer d = gableDormer();
    d.material = "tile";
    d.roofMaterial = "roof_tiles";
    const BuildPlan plan = makeBuildPlan(withDormer(d), sampleAssetSource(), sampleMaterialSource());
    REQUIRE(plan.floorParts[0].size() == 1);
    std::set<std::string> slots;
    for (const PartFace& face : plan.floorParts[0][0].faces) slots.insert(face.slot);
    REQUIRE(slots.size() == 2);
    CHECK(slots.count(std::string(kExteriorWallSlot) + "_Tile") == 1);
    CHECK(std::any_of(slots.begin(), slots.end(), [](const std::string& s) { return s.rfind(std::string(kRoofSlot), 0) == 0; }));
}

TEST_CASE("a dormer off every pitched roof is not built", "[core][dormers]") {
    checkLeftOut(withDormer(gableDormer("dm1", 20.0, 20.0)), "dormer_not_on_roof");
    Project flat = withDormer(gableDormer());
    flat.plan.roof.type = "flat";
    checkLeftOut(flat, "dormer_not_on_roof");
    CHECK(placementOf(flat).status.code == ErrorCode::InvalidValue);
    CHECK(placementOf(flat).status.message == "Dormer dm1 does not stand on a pitched roof of floor f1.");
}

TEST_CASE("a dormer past the ridge, the eaves or the section's edge is not built", "[core][dormers]") {
    Dormer deep = gableDormer();
    deep.depth = 3.5;
    checkLeftOut(withDormer(deep), "dormer_too_big");
    checkLeftOut(withDormer(gableDormer("dm1", 0.5, 5.5)), "dormer_too_big");
    checkLeftOut(withDormer(gableDormer("dm1", 4.0, 6.2)), "dormer_too_big");
    CHECK(placementOf(withDormer(deep)).status.message.find("does not fit its roof") != std::string::npos);
}

TEST_CASE("a dormer across a valley is not built", "[core][dormers]") {
    Project project = makeNewProject();
    project.plan = lPlan();
    project.plan.roof.type = "hip";
    project.plan.roof.pitch = 45.0;
    clearMaterialDefaults(project);
    Dormer d = gableDormer("dm1", 4.2, 3.8);
    d.depth = 1.5;
    d.windowHeight = 0.6;
    checkLeftOut(withDormer(d, project), "dormer_too_big");

    d.x = 5.0;
    const Project clear = withDormer(d, project);
    CHECK(placementOf(clear).status.ok());
    CHECK(problemsOf(clear).empty());
}

TEST_CASE("a tall window makes the dormer reach further back, up to the ridge", "[core][dormers]") {
    Dormer taller = gableDormer();
    taller.windowHeight = 1.5;
    const Project fits = withDormer(taller);
    const DormerPlacement place = placementOf(fits);
    REQUIRE(place.status.ok());
    const double depth = (1.8 + 0.8 * kTan30) / 1.0;
    const auto backmost = std::max_element(place.footprint.begin(), place.footprint.end(),
                                           [](const Vec2& a, const Vec2& b) { return a.y < b.y; });
    CHECK(backmost->y == Approx(-5.5 + depth));
    CHECK(problemsOf(fits).empty());

    Dormer tall = gableDormer();
    tall.windowHeight = 2.5;
    checkLeftOut(withDormer(tall), "dormer_too_big");
    CHECK(placementOf(withDormer(tall)).status.message.find("its window needs it to reach") != std::string::npos);
}

TEST_CASE("a dormer with the default values builds on gable roofs of 30, 35 and 45 degrees", "[core][dormers]") {
    for (const double pitch : {30.0, 35.0, 45.0}) {
        CAPTURE(pitch);
        Project project = makeNewProject();
        addBoxRoom(project.plan.floors[0], 1, 0.0, 0.0, 10.0, 8.0);
        project.plan.roof.type = "gable";
        project.plan.roof.pitch = pitch;
        clearMaterialDefaults(project);
        Dormer d;
        d.id = "dm1";
        d.x = 5.0;
        d.y = 7.6;
        const Project built = withDormer(d, project);
        CHECK(placementOf(built).status.ok());
        const BuildPlan plan = makeBuildPlan(built, sampleAssetSource(), sampleMaterialSource());
        CHECK(dormerProblems(plan).empty());
        REQUIRE(plan.floorParts[0].size() == 1);
        checkPlanar(plan.floorParts[0][0].faces);
        checkClosed(plan.floorParts[0][0].faces);
        CHECK(plan.roofCuts.size() == 1);
    }
}

TEST_CASE("a dormer whose window does not fit its front is not built", "[core][dormers]") {

    Dormer wide = gableDormer();
    wide.windowWidth = 1.55;
    checkLeftOut(withDormer(wide), "dormer_window_too_big");

    Dormer steep = shedDormer();
    steep.pitch = 50.0;
    checkLeftOut(withDormer(steep), "dormer_window_too_big");
    CHECK(placementOf(withDormer(steep)).status.message.find("lower pitch") != std::string::npos);
}

TEST_CASE("a dormer of an unknown type is not built", "[core][dormers]") {
    Dormer d = gableDormer();
    d.type = "eyebrow";
    checkLeftOut(withDormer(d), "dormer_type_unknown");
}

TEST_CASE("the later of two overlapping dormers is not built", "[core][dormers]") {
    Project project = withDormer(gableDormer());
    project.plan.floors[0].dormers.push_back(gableDormer("dm2", 4.5, 5.5));
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    const std::vector<BuildProblem> problems = dormerProblems(plan);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "dormer_overlap");
    CHECK(problems[0].object == "dm2");
    REQUIRE(plan.floorParts[0].size() == 1);
    CHECK(plan.floorParts[0][0].id == "dm1");
    CHECK(plan.roofCuts.size() == 1);

    Project apart = withDormer(gableDormer());
    apart.plan.floors[0].dormers.push_back(gableDormer("dm2", 6.2, 5.5));
    CHECK(problemsOf(apart).empty());
}

TEST_CASE("a dormer stays in the plan when its roof goes, and the build reports it", "[core][dormers]") {
    Project project = withDormer(gableDormer());
    project.plan.roof.type = "flat";
    CHECK(checkPlan(project.plan).ok());
    CHECK(project.plan.floors[0].dormers.size() == 1);
    checkLeftOut(project, "dormer_not_on_roof");
}

TEST_CASE("checkDormer uses the rules and sentences of checkPlan", "[core][dormers]") {
    CHECK(checkDormer(gableDormer()).ok());
    Dormer noId = gableDormer();
    noId.id.clear();
    CHECK(checkDormer(noId).ok());

    Dormer narrow = gableDormer();
    narrow.width = 0.4;
    const Status s = checkDormer(narrow);
    CHECK(s.code == ErrorCode::InvalidValue);
    CHECK(s.message == "The dormer width must be from 0.5 to 6 metres.");
    CHECK(s.where.empty());

    Dormer flat = gableDormer();
    flat.pitch = 2.0;
    CHECK(checkDormer(flat).code == ErrorCode::InvalidValue);
    Dormer badType = gableDormer();
    badType.type = "Bad Type";
    CHECK(checkDormer(badType).code == ErrorCode::InvalidValue);
    Dormer bigWindow = gableDormer();
    bigWindow.windowWidth = 2.0;
    CHECK(checkDormer(bigWindow).code == ErrorCode::InvalidValue);
}
