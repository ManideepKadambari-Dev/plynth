#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildParts.h>
#include <plynth/core/Canopies.h>
#include <plynth/core/Collision.h>
#include <plynth/core/MaterialDefaults.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Storeys.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// A floor whose walls, 0.2 m thick, run round the box from (x0, y0) to (x1, y1) on the plan, closing one room.
// Every id starts with the floor's id, so floors made by it never share one.
Floor boxFloor(const std::string& id, double x0, double y0, double x1, double y1)
{
    Floor f;
    f.id = id;
    f.name = id;
    f.points = {{id + "p1", x0, y0, 0.0}, {id + "p2", x1, y0, 0.0}, {id + "p3", x1, y1, 0.0}, {id + "p4", x0, y1, 0.0}};
    Room room;
    room.id = id + "r1";
    for (int i = 0; i < 4; ++i) {
        Wall w;
        w.id = id + "w" + std::to_string(i + 1);
        w.from = id + "p" + std::to_string(i + 1);
        w.to = id + "p" + std::to_string(i == 3 ? 1 : i + 2);
        w.thickness = 0.2;
        f.walls.push_back(w);
        room.walls.push_back(w.id);
    }
    f.rooms = {room};
    return f;
}

// A project of the given floors, with no material defaults and a flat roof with a parapet.
Project projectOf(std::vector<Floor> floors)
{
    Project project = makeNewProject();
    project.plan.floors = std::move(floors);
    project.plan.roof = RoofSettings{};
    project.plan.roof.type = "flat";
    project.plan.roof.parapet = 0.6;
    clearMaterialDefaults(project);
    return project;
}

// The one storey house of a 4 by 4 box.
Project houseProject()
{
    return projectOf({boxFloor("f1", 0.0, 0.0, 4.0, 4.0)});
}

// A canopy over a box on the plan, with the canopy defaults.
Canopy canopyOver(const std::string& id, double x0, double y0, double x1, double y1)
{
    Canopy c;
    c.id = id;
    c.outline = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    return c;
}

// The part a floor got with this object name. Null when there is none.
const BuildPart* partNamed(const BuildPlan& plan, std::size_t floor, const std::string& object)
{
    if (floor >= plan.floorParts.size()) return nullptr;
    for (const BuildPart& part : plan.floorParts[floor]) {
        if (part.object == object) return &part;
    }
    return nullptr;
}

// The area of a ring seen from above, positive when it turns left.
double fromAbove(const std::vector<Vec3>& ring)
{
    double twice = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return 0.5 * twice;
}

// The area seen from above of every face of a part in a slot, holes taken off.
double areaInSlot(const BuildPart& part, const std::string& slot)
{
    double area = 0.0;
    for (const PartFace& face : part.faces) {
        if (face.slot != slot) continue;
        double size = fromAbove(face.outline);
        for (const auto& hole : face.holes) size += fromAbove(hole);
        area += std::abs(size);
    }
    return area;
}

// How many faces of a part are in a slot.
std::size_t facesInSlot(const BuildPart& part, const std::string& slot)
{
    return static_cast<std::size_t>(
        std::count_if(part.faces.begin(), part.faces.end(), [&](const PartFace& f) { return f.slot == slot; }));
}

// The highest point of a part's faces.
double highest(const BuildPart& part)
{
    double z = -1e300;
    for (const PartFace& face : part.faces) {
        for (const Vec3& p : face.outline) z = std::max(z, p.z);
    }
    return z;
}

// The lowest point of a part's faces.
double lowest(const BuildPart& part)
{
    double z = 1e300;
    for (const PartFace& face : part.faces) {
        for (const Vec3& p : face.outline) z = std::min(z, p.z);
    }
    return z;
}

// The codes of the problems about one object.
std::vector<std::string> codesFor(const BuildPlan& plan, const std::string& object)
{
    std::vector<std::string> out;
    for (const BuildProblem& p : plan.problems) {
        if (p.object == object) out.push_back(p.code);
    }
    return out;
}

// Says whether a list holds a text.
bool holds(const std::vector<std::string>& list, const std::string& text)
{
    return std::find(list.begin(), list.end(), text) != list.end();
}

// Says whether a face stands in the world plane y = at, every point on it.
bool standsAt(const PartFace& face, double at)
{
    return std::all_of(face.outline.begin(), face.outline.end(), [&](const Vec3& p) { return std::abs(p.y - at) < 1e-6; });
}

// Says whether a slot is listed with this material and neutral surface.
bool hasSlot(const BuildPlan& plan, const std::string& name, const std::string& material, const std::string& surface)
{
    return std::any_of(plan.materialSlots.begin(), plan.materialSlots.end(), [&](const BuildSlot& s) {
        return s.name == name && s.material == material && s.surface == surface;
    });
}

}

TEST_CASE("the canopy and overhang makers are registered for the floors group", "[core][canopies]") {
    const std::vector<const PartMaker*> makers = allPartMakers();
    const auto find = [&](std::string_view id) -> const PartMaker* {
        for (const PartMaker* m : makers) {
            if (m->id() == id) return m;
        }
        return nullptr;
    };
    const PartMaker* canopies = find("canopies");
    const PartMaker* overhangs = find("overhangs");
    REQUIRE(canopies);
    REQUIRE(overhangs);
    CHECK(canopies->order() == 40);
    CHECK(overhangs->order() == 41);
    CHECK(canopies->kinds() == std::vector<std::string>{"canopy"});
    CHECK(overhangs->kinds() == std::vector<std::string>{"overhang"});
    CHECK(canopies->exportGroup() == "surfaces");
    CHECK(overhangs->exportGroup() == "surfaces");
}

TEST_CASE("checkCanopy takes good values and refuses bad ones", "[core][canopies]") {
    const Canopy good = canopyOver("cn1", 0.0, 0.0, 2.0, 1.0);
    requireOk(checkCanopy(good));

    const auto refused = [](Canopy c) { return checkCanopy(c).code == ErrorCode::InvalidValue; };
    Canopy c = good;
    c.outline.resize(2);
    CHECK(refused(c));
    c = good;
    c.outline = {{0.0, 0.0}, {0.05, 0.0}, {0.05, 0.05}};
    CHECK(refused(c));
    c = good;
    c.outline = {{0.0, 0.0}, {4.0, 0.0}, {1.0, 2.0}, {3.0, 2.0}};
    CHECK(checkCanopy(c).message == "The canopy outline crosses itself.");
    c = good;
    c.outline[1].x = std::nan("");
    CHECK(refused(c));
    c = good;
    c.height = 0.5;
    CHECK(refused(c));
    c.height = 21.0;
    CHECK(refused(c));
    c = good;
    c.thickness = 0.01;
    CHECK(refused(c));
    c.thickness = 1.5;
    CHECK(refused(c));
    c = good;
    c.roof.type = "nonesuch";
    CHECK(refused(c));
    c = good;
    c.roof.type = "gable";
    c.roof.pitch = 2.0;
    CHECK(refused(c));
    c.roof.pitch = 30.0;
    requireOk(checkCanopy(c));
    c.roof.overhang = 3.0;
    CHECK(refused(c));
    c = good;
    c.material = "bad name";
    CHECK(refused(c));
    c = good;
    c.undersideMaterial = "";
    CHECK(refused(c));
    c = good;
    c.fasciaMaterial = "oak_planks";
    requireOk(checkCanopy(c));

    // A clockwise outline and a point drawn twice are fine.
    c = good;
    std::reverse(c.outline.begin(), c.outline.end());
    c.outline.push_back(c.outline.back());
    requireOk(checkCanopy(c));
}

TEST_CASE("a flat canopy with no parapet is a plain slab with soffit and fascia", "[core][canopies]") {
    Project project = houseProject();
    project.plan.floors[0].canopies = {canopyOver("cn1", 6.0, 0.0, 9.0, 2.0)};
    const BuildPlan plan = makeBuildPlan(project, {});

    const BuildPart* part = partNamed(plan, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    CHECK(part->kind == "canopy");
    CHECK(part->id == "cn1");
    CHECK(part->prisms.empty());
    CHECK(areaInSlot(*part, "Flat_Roof") == Approx(6.0));
    CHECK(areaInSlot(*part, "Soffit") == Approx(6.0));
    CHECK(facesInSlot(*part, "Fascia") == 4);
    CHECK(lowest(*part) == Approx(2.7));
    CHECK(highest(*part) == Approx(2.95));
    for (const PartFace& face : part->faces) {
        if (face.slot == "Flat_Roof") CHECK(fromAbove(face.outline) > 0.0);
        if (face.slot == "Soffit") CHECK(fromAbove(face.outline) < 0.0);
    }

    double pieces = 0.0;
    for (const CollisionPiece& piece : part->collision) {
        CHECK(isConvexRing(piece.outline));
        CHECK(piece.bottom == Approx(2.7));
        CHECK(piece.top == Approx(2.95));
        pieces += signedArea(piece.outline);
    }
    CHECK(pieces == Approx(6.0));
    CHECK(part->hulls.empty());

    // Free standing with nothing under it.
    CHECK(codesFor(plan, "cn1") == std::vector<std::string>{"canopy_unsupported"});
    for (const BuildProblem& p : plan.problems) {
        if (p.object == "cn1") CHECK(p.severity == BuildSeverity::Warning);
    }
}

TEST_CASE("a flat canopy's overhang grows its slab", "[core][canopies]") {
    Project project = houseProject();
    Canopy c = canopyOver("cn1", 6.0, 0.0, 9.0, 2.0);
    c.roof.overhang = 0.5;
    project.plan.floors[0].canopies = {c};
    const BuildPlan plan = makeBuildPlan(project, {});
    const BuildPart* part = partNamed(plan, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    CHECK(areaInSlot(*part, "Flat_Roof") == Approx(4.0 * 3.0));
}

TEST_CASE("a canopy against the house stops at the outer wall face with no fascia there", "[core][canopies]") {
    Project project = houseProject();
    project.plan.floors[0].canopies = {canopyOver("cn1", 1.0, 3.5, 3.0, 6.0)};
    const BuildPlan plan = makeBuildPlan(project, {});
    const BuildPart* part = partNamed(plan, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    CHECK(areaInSlot(*part, "Soffit") == Approx(2.0 * 1.9));
    CHECK(facesInSlot(*part, "Fascia") == 3);
    for (const PartFace& face : part->faces) CHECK_FALSE(standsAt(face, -4.1));
    // The wall holds it up.
    CHECK(codesFor(plan, "cn1").empty());
}

TEST_CASE("a canopy that reaches the main roof's eaves is cut back to them", "[core][canopies]") {
    Project project = houseProject();
    project.plan.roof.type = "gable";
    project.plan.roof.pitch = 30.0;
    project.plan.roof.overhang = 0.5;
    project.plan.floors[0].canopies = {canopyOver("cn1", 1.0, 3.5, 3.0, 6.0)};
    const BuildPlan level = makeBuildPlan(project, {});
    const BuildPart* part = partNamed(level, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    CHECK(areaInSlot(*part, "Soffit") == Approx(2.0 * 1.4));
    for (const PartFace& face : part->faces) {
        if (face.slot == "Fascia") CHECK_FALSE(standsAt(face, -4.6));
    }

    // Lower than the eaves, it runs under them up to the wall.
    project.plan.floors[0].canopies[0].height = 2.0;
    const BuildPlan lower = makeBuildPlan(project, {});
    const BuildPart* under = partNamed(lower, 0, "f1_cn1_Canopy");
    REQUIRE(under);
    CHECK(areaInSlot(*under, "Soffit") == Approx(2.0 * 1.9));
}

TEST_CASE("a gable canopy sits on its slab with gables, covering hulls and the soffit", "[core][canopies]") {
    Project project = houseProject();
    Canopy c = canopyOver("cn1", 6.0, 0.0, 9.0, 2.0);
    c.roof.type = "gable";
    c.roof.pitch = 30.0;
    project.plan.floors[0].canopies = {c};
    const BuildPlan plan = makeBuildPlan(project, {});
    const BuildPart* part = partNamed(plan, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    const double rise = std::tan(30.0 * 3.14159265358979323846 / 180.0);
    CHECK(areaInSlot(*part, "Roof") == Approx(6.0));
    CHECK(areaInSlot(*part, "Soffit") >= 6.0 - 1e-6);
    CHECK(facesInSlot(*part, "Exterior_Wall") >= 2);
    CHECK(lowest(*part) == Approx(2.7));
    CHECK(highest(*part) == Approx(2.95 + rise));
    REQUIRE(part->hulls.size() == 2);
    for (const auto& hull : part->hulls) CHECK(hull.size() >= 8);
    CHECK_FALSE(part->collision.empty());

    // A thin canopy still keeps the pitched roof's thickness above its underside.
    project.plan.floors[0].canopies[0].thickness = 0.1;
    const BuildPlan thin = makeBuildPlan(project, {});
    const BuildPart* thinPart = partNamed(thin, 0, "f1_cn1_Canopy");
    REQUIRE(thinPart);
    CHECK(lowest(*thinPart) == Approx(2.7));
    CHECK(highest(*thinPart) == Approx(2.7 + kRoofThickness + rise));
}

TEST_CASE("a gable canopy against the house has no gable fill on the wall", "[core][canopies]") {
    Project project = houseProject();
    Canopy c = canopyOver("cn1", 1.0, 4.0, 3.0, 7.0);
    c.roof.type = "gable";
    c.roof.pitch = 30.0;
    c.roof.direction = 90.0;
    project.plan.floors[0].canopies = {c};
    const BuildPlan plan = makeBuildPlan(project, {});
    const BuildPart* part = partNamed(plan, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    CHECK(facesInSlot(*part, "Exterior_Wall") == 1);
    for (const PartFace& face : part->faces) CHECK_FALSE(standsAt(face, -4.1));
}

TEST_CASE("a canopy of an unknown roof type is built flat with a warning", "[core][canopies]") {
    Project project = houseProject();
    Canopy c = canopyOver("cn1", 6.0, 0.0, 9.0, 2.0);
    c.roof.type = "nonesuch";
    project.plan.floors[0].canopies = {c};
    const BuildPlan plan = makeBuildPlan(project, {});
    const BuildPart* part = partNamed(plan, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    CHECK(areaInSlot(*part, "Flat_Roof") == Approx(6.0));
    CHECK(holds(codesFor(plan, "cn1"), "canopy_type_fallback"));
}

TEST_CASE("a canopy whose outline crosses itself or lies inside the house is left out", "[core][canopies]") {
    Project project = houseProject();
    Canopy crossing = canopyOver("cn1", 6.0, 0.0, 9.0, 2.0);
    crossing.outline = {{6.0, 0.0}, {9.0, 2.0}, {9.0, 0.0}, {6.0, 2.0}};
    project.plan.floors[0].canopies = {crossing, canopyOver("cn2", 1.0, 1.0, 3.0, 3.0)};
    const BuildPlan plan = makeBuildPlan(project, {});
    CHECK_FALSE(partNamed(plan, 0, "f1_cn1_Canopy"));
    CHECK_FALSE(partNamed(plan, 0, "f1_cn2_Canopy"));
    CHECK(codesFor(plan, "cn1") == std::vector<std::string>{"canopy_outline_invalid"});
    CHECK(codesFor(plan, "cn2") == std::vector<std::string>{"canopy_outline_invalid"});
    for (const BuildProblem& p : plan.problems) {
        if (p.code == "canopy_outline_invalid") {
            CHECK(p.severity == BuildSeverity::Error);
            CHECK(p.floor == "f1");
            CHECK(p.step == kCheckStep);
        }
    }
}

TEST_CASE("a post or a deck under a canopy holds it up", "[core][canopies]") {
    Project project = houseProject();
    project.plan.floors[0].canopies = {canopyOver("cn1", 6.0, 0.0, 9.0, 2.0)};

    Project posted = project;
    Item post;
    post.id = "i1";
    post.x = 9.2;
    post.y = 1.0;
    post.size = {0.2, 0.2, 2.7};
    post.generator = GeneratorUse{"column_round", {}};
    posted.plan.floors[0].items = {post};
    CHECK_FALSE(holds(codesFor(makeBuildPlan(posted, {}), "cn1"), "canopy_unsupported"));

    Project far = posted;
    far.plan.floors[0].items[0].x = 9.5;
    CHECK(holds(codesFor(makeBuildPlan(far, {}), "cn1"), "canopy_unsupported"));

    Project decked = project;
    Deck deck;
    deck.id = "dk1";
    deck.outline = {{6.0, 2.2}, {9.0, 2.2}, {9.0, 4.0}, {6.0, 4.0}};
    decked.plan.floors[0].decks = {deck};
    CHECK_FALSE(holds(codesFor(makeBuildPlan(decked, {}), "cn1"), "canopy_unsupported"));
}

TEST_CASE("a canopy's materials go into its slots", "[core][canopies]") {
    Project project = houseProject();
    Canopy c = canopyOver("cn1", 6.0, 0.0, 9.0, 2.0);
    c.material = "concrete";
    c.undersideMaterial = "oak_planks";
    c.fasciaMaterial = "painted_wood";
    project.plan.floors[0].canopies = {c};
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    const BuildPart* part = partNamed(plan, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    for (const PartFace& face : part->faces) {
        if (fromAbove(face.outline) > 1e-9) CHECK(hasSlot(plan, face.slot, "concrete", "Flat_Roof"));
        else if (fromAbove(face.outline) < -1e-9) CHECK(hasSlot(plan, face.slot, "oak_planks", "Soffit"));
        else CHECK(hasSlot(plan, face.slot, "painted_wood", "Fascia"));
    }
}

TEST_CASE("a canopy goes into the job in the parts form", "[core][canopies][schema]") {
    TempDir tmp("canopy-job");
    Project project = houseProject();
    Canopy c = canopyOver("cn1", 6.0, 0.0, 9.0, 2.0);
    c.roof.type = "gable";
    project.plan.floors[0].canopies = {c};
    const BuildPlan plan = makeBuildPlan(project, {});
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    const Json& floor = job["params"]["floors"][0];
    REQUIRE(floor.contains("parts"));
    REQUIRE(floor["parts"].size() == 1);
    CHECK(floor["parts"][0]["object"] == "f1_cn1_Canopy");
    CHECK(floor["parts"][0]["plynth_kind"] == "canopy");
    CHECK(floor["parts"][0]["plynth_id"] == "cn1");
    nlohmann::json parts = nlohmann::json::object();
    parts["parts"] = nlohmann::json::parse(floor["parts"].dump());
    const std::string problem = schemaProblem("blender-job-parts.schema.json", parts);
    INFO(problem);
    CHECK(problem.empty());
}

TEST_CASE("an upper floor that overhangs gets a fascia board and a soffit underneath", "[core][canopies][overhang]") {
    Project project = projectOf({boxFloor("f1", 0.0, 0.0, 4.0, 4.0), boxFloor("f2", 0.0, 0.0, 6.0, 4.0)});
    const BuildPlan plan = makeBuildPlan(project, {});
    REQUIRE(plan.floors.size() == 2);
    CHECK_FALSE(plan.floors[0].slab.undersides.empty());
    CHECK(plan.floorMaterials[0].slab.undersideSlot == "Soffit");

    const BuildPart* board = partNamed(plan, 0, "f1_Overhang");
    REQUIRE(board);
    CHECK(board->kind == "overhang");
    CHECK(board->id == "f1");
    CHECK(board->collision.empty());
    double area = 0.0;
    for (const PartPrism& prism : board->prisms) {
        CHECK(prism.slot == "Fascia");
        CHECK(prism.bottom == Approx(3.0));
        CHECK(prism.top == Approx(3.2));
        area += signedArea(prism.outline);
        for (const auto& hole : prism.holes) area += signedArea(hole);
    }
    // The right edge, 4.24 m with the corners, and the two side edges of 2.02 m, all 0.02 m deep.
    CHECK(area == Approx(0.02 * (4.24 + 2.0 * 2.02)).margin(1e-6));
    CHECK(partNamed(plan, 1, "f2_Overhang") == nullptr);
    CHECK(codesFor(plan, "f2").empty());
    CHECK(plan.roofCuts.empty());
}

TEST_CASE("an upper floor without an overhang gets no board", "[core][canopies][overhang]") {
    Project project = projectOf({boxFloor("f1", 0.0, 0.0, 6.0, 4.0), boxFloor("f2", 0.0, 0.0, 4.0, 4.0)});
    const BuildPlan plan = makeBuildPlan(project, {});
    for (const auto& parts : plan.floorParts) CHECK(parts.empty());
}

TEST_CASE("an overhang deeper than the limit is reported with its depth", "[core][canopies][overhang]") {
    Project project = projectOf({boxFloor("f1", 0.0, 0.0, 4.0, 4.0), boxFloor("f2", 0.0, 0.0, 8.5, 4.0)});
    const BuildPlan plan = makeBuildPlan(project, {});
    std::vector<BuildProblem> found;
    for (const BuildProblem& p : plan.problems) {
        if (p.code == "floor_overhang") found.push_back(p);
    }
    REQUIRE(found.size() == 1);
    CHECK(found[0].severity == BuildSeverity::Warning);
    CHECK(found[0].floor == "f2");
    CHECK(found[0].object == "f2");
    CHECK(found[0].message.find("4.50 m") != std::string::npos);
    CHECK(partNamed(plan, 0, "f1_Overhang"));
}

TEST_CASE("a lower roof that would rise into an overhang is cut back under it", "[core][canopies][overhang]") {
    Project project = projectOf({boxFloor("f1", 0.0, 0.0, 8.0, 4.0), boxFloor("f2", 0.0, 0.0, 4.0, 4.0),
                                 boxFloor("f3", 0.0, 0.0, 8.0, 4.0)});
    project.plan.roof.type = "gable";
    project.plan.roof.pitch = 60.0;
    project.plan.roof.overhang = 0.3;
    const BuildPlan plan = makeBuildPlan(project, {});
    REQUIRE(plan.floors.size() == 3);
    REQUIRE_FALSE(plan.floors[0].roofs.empty());
    const double underside = 3.2 + 3.0;

    // Strictly inside the overhang of f3 over f2, seen from above.
    const auto inside = [](double x, double y) { return x > 4.1 + 1e-6 && x < 8.1 - 1e-6 && y > -4.1 + 1e-6 && y < 0.1 - 1e-6; };
    bool anyCovering = false;
    for (const RoofBuild& roof : plan.floors[0].roofs) {
        for (const RoofFace& face : roof.faces) {
            if (face.part == RoofPart::Covering) anyCovering = true;
            for (const Vec3& p : face.outline) {
                if (inside(p.x, p.y)) CHECK(p.z <= underside + 1e-6);
            }
        }
    }
    CHECK(anyCovering);
    for (const CollisionShape& shape : plan.collision[0]) {
        for (const auto& hull : shape.hulls) {
            for (const Vec3& p : hull) {
                if (inside(p.x, p.y)) CHECK(p.z <= underside + 1e-6);
            }
        }
    }
    CHECK(std::any_of(plan.roofCuts.begin(), plan.roofCuts.end(), [](const RoofCut& c) { return c.floor == "f1"; }));
    CHECK(partNamed(plan, 1, "f2_Overhang"));

    // A roof that stays below the overhang is left whole.
    project.plan.roof.pitch = 30.0;
    CHECK(makeBuildPlan(project, {}).roofCuts.empty());
}

TEST_CASE("a canopy that reaches an upper floor's slab stops at that floor", "[core][canopies][overhang]") {
    Project project = projectOf({boxFloor("f1", 0.0, 0.0, 4.0, 4.0), boxFloor("f2", 0.0, 0.0, 6.0, 4.0)});
    Canopy high = canopyOver("cn1", 4.5, 1.0, 5.5, 2.0);
    high.height = 2.9;
    project.plan.floors[0].canopies = {high};
    const BuildPlan blocked = makeBuildPlan(project, {});
    CHECK_FALSE(partNamed(blocked, 0, "f1_cn1_Canopy"));
    CHECK(holds(codesFor(blocked, "cn1"), "canopy_outline_invalid"));

    project.plan.floors[0].canopies[0].height = 2.0;
    const BuildPlan below = makeBuildPlan(project, {});
    const BuildPart* part = partNamed(below, 0, "f1_cn1_Canopy");
    REQUIRE(part);
    CHECK(areaInSlot(*part, "Soffit") == Approx(1.0));
}
