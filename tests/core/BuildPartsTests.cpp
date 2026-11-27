#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include "../../core/src/parts/RoofCuts.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildParts.h>
#include <plynth/core/BuildPreview.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/MaterialDefaults.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// What the test maker does while a test has it on.
struct BoxSettings {
    bool on = false;
    std::optional<std::string> deckMaterial; // the box's top; empty for the project default
    bool sideDeck = false;                   // its sides in the deck surface with tile instead of glass
    bool cut = false;                        // cut a 1 m square out of the floor's first roof section
    bool bad = false;                        // also give a part with a bad object name and one with a taken name
};

BoxSettings& box()
{
    static BoxSettings settings;
    return settings;
}

// Turns the test maker on for one test and off again at its end.
struct BoxOn {
    explicit BoxOn(BoxSettings settings = {})
    {
        settings.on = true;
        box() = settings;
    }
    ~BoxOn() { box() = BoxSettings{}; }
    BoxOn(const BoxOn&) = delete;
    BoxOn& operator=(const BoxOn&) = delete;
};

// A square ring in world X and Y, anticlockwise.
std::vector<Vec2> square(double x0, double y0, double x1, double y1)
{
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// A part maker that makes one box on the first floor, a face on the site and a strip light, only while a test
// turns it on, so every other test sees no parts at all.
class TestBoxMaker final : public PartMaker {
public:
    std::string_view id() const override { return "test_box"; }
    int order() const override { return 900; }
    std::vector<std::string> kinds() const override
    {
        if (!box().on) return {};
        return {"test_box"};
    }
    std::string_view exportGroup() const override { return "surfaces"; }

    // The box, its light and its cut on the first floor.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!box().on || input.floorIndex != 0) return out;
        const Project& project = *input.project;
        const double level = (*input.levels)[input.floorIndex];
        BuildPart part;
        part.object = input.floor->id + "_TestBox";
        part.kind = "test_box";
        part.id = input.floor->id;
        PartPrism prism;
        prism.outline = square(0.5, -1.5, 1.5, -0.5);
        prism.bottom = level;
        prism.top = level + 1.0;
        prism.slot = partSlot(project, Surface::Deck, box().deckMaterial).slot;
        prism.sideSlot = box().sideDeck ? partSlot(project, Surface::Deck, std::string("tile")).slot
                                        : partSlot(project, Surface::Glass, std::string("tile")).slot;
        part.prisms.push_back(prism);
        part.collision.push_back({prism.outline, prism.bottom, prism.top});
        out.parts.push_back(part);

        if (box().bad) {
            BuildPart badName = part;
            badName.object = "bad name";
            out.parts.push_back(badName);
            out.parts.push_back(part);
        }

        StripLight light;
        light.id = "ls1";
        light.start = {0.5, -0.5, level + 2.0};
        light.end = {1.5, -0.5, level + 2.0};
        light.facing = {0.0, 0.0, -2.0};
        light.power = 5.0;
        out.lights.push_back(light);

        if (box().cut && !input.floors->back().roofs.empty()) {
            RoofCut cut;
            cut.floor = input.floor->id;
            cut.index = input.floors->back().roofs.front().index;
            cut.cutter.outline = square(1.5, -2.5, 2.5, -1.5);
            cut.cutter.bottom = -100.0;
            cut.cutter.top = 100.0;
            out.roofCuts.push_back(cut);
        }
        return out;
    }

    // One flat face on the site in a plain slot.
    PartOutput siteParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!box().on || input.floor != nullptr) return out;
        BuildPart part;
        part.object = "Site_TestBox";
        part.kind = "test_box";
        part.id = "site";
        part.uv = PartUv::Along;
        part.bevel = false;
        PartFace face;
        face.outline = {{-1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 2.0, 0.0}, {-1.0, 2.0, 0.0}};
        face.slot = "Light_Strip";
        part.faces.push_back(face);
        part.hulls.push_back({{-1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 2.0, 0.0}, {-1.0, 1.0, 0.1}});
        out.parts.push_back(part);
        return out;
    }
};

const PartMakerRegistrar registrar(std::make_unique<TestBoxMaker>());

// The build project with the deck default set, so the box's top has a material.
Project boxProject()
{
    Project project = buildProject();
    setMaterialDefault(project.settings.materials, Surface::Deck, std::string(kSampleOakPlanks));
    return project;
}

// Says whether the slot table has a slot with this name, material and neutral surface.
bool hasSlot(const BuildPlan& plan, const std::string& name, const std::string& material, const std::string& surface)
{
    return std::any_of(plan.materialSlots.begin(), plan.materialSlots.end(), [&](const BuildSlot& s) {
        return s.name == name && s.material == material && s.surface == surface;
    });
}

// The area of a ring of points seen from above, positive when it turns left.
double areaFromAbove(const std::vector<Vec3>& ring)
{
    double twice = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return 0.5 * twice;
}

// The area of a face seen from above, its holes taken off.
double faceAreaFromAbove(const RoofFace& face)
{
    double area = areaFromAbove(face.outline);
    for (const auto& hole : face.holes) area += areaFromAbove(hole);
    return area;
}

// The area of a face standing in the plane x = 0, seen from +x (y to the right, z up), its holes taken off.
double areaFromPlusX(const RoofFace& face)
{
    const auto ring = [](const std::vector<Vec3>& points) {
        double twice = 0.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Vec3& a = points[i];
            const Vec3& b = points[(i + 1) % points.size()];
            twice += a.y * b.z - b.y * a.z;
        }
        return 0.5 * twice;
    };
    double area = ring(face.outline);
    for (const auto& hole : face.holes) area += ring(hole);
    return area;
}

// The covering area of every roof section of a floor, seen from above.
double coveringArea(const FloorBuild& floor)
{
    double area = 0.0;
    for (const RoofBuild& roof : floor.roofs) {
        for (const RoofFace& face : roof.faces) {
            if (face.part == RoofPart::Covering) area += faceAreaFromAbove(face);
        }
    }
    return area;
}

// The parts of a job in the form of schema/blender-job-parts.schema.json.
nlohmann::json partsOf(const Json& job)
{
    Json out = Json::object();
    const Json& params = job["params"];
    if (params["floors"][0].contains("parts")) out["parts"] = params["floors"][0]["parts"];
    if (params["floors"][0].contains("strip_lights")) out["strip_lights"] = params["floors"][0]["strip_lights"];
    if (params.contains("site_parts")) out["site_parts"] = params["site_parts"];
    return nlohmann::json::parse(out.dump());
}

}

TEST_CASE("the part maker registry keeps one maker per well formed id", "[core][parts]") {
    const std::vector<const PartMaker*> makers = allPartMakers();
    REQUIRE(std::any_of(makers.begin(), makers.end(), [](const PartMaker* m) { return m->id() == "test_box"; }));
    CHECK_FALSE(registerPartMaker(nullptr));
    CHECK_FALSE(registerPartMaker(std::make_unique<TestBoxMaker>()));
    CHECK(allPartMakers().size() == makers.size());
}

TEST_CASE("partSlot names the surface's slot with the plan's material or the project default", "[core][parts]") {
    const Project project = makeNewProject();
    const PartSlot deck = partSlot(project, Surface::Deck, std::nullopt);
    CHECK(deck.material == std::string(kSampleOakPlanks));
    CHECK(deck.slot == "Deck|Deck|oak_planks");
    CHECK(partSlot(project, Surface::Deck, std::string("tile")).slot == "Deck|Deck|tile");
    CHECK(partSlot(project, Surface::WallOutside, std::string("tile")).slot == "Exterior_Wall|Wall_Outside|tile");
    CHECK(partSlot(project, Surface::BoundaryWall, std::string("tile")).slot == "Boundary_Wall|Boundary_Wall|tile");

    Project bare = project;
    setMaterialDefault(bare.settings.materials, Surface::Ground, std::nullopt);
    const PartSlot ground = partSlot(bare, Surface::Ground, std::nullopt);
    CHECK(ground.material.empty());
    CHECK(ground.slot == "Ground|Ground|");

    const PartSlot soffit = partSlot("Soffit", "Soffit", std::nullopt);
    CHECK(soffit.slot == "Soffit|Soffit|");
    CHECK(soffit.material.empty());
}

TEST_CASE("without a maker that gives anything the plan and job have no parts", "[core][parts]") {
    plynth::test::TempDir tmp("parts-none");
    const Project project = boxProject();
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    REQUIRE(plan.floorParts.size() == plan.floors.size());
    REQUIRE(plan.stripLights.size() == plan.floors.size());
    for (const auto& parts : plan.floorParts) CHECK(parts.empty());
    CHECK(plan.siteParts.empty());
    CHECK(plan.roofCuts.empty());
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    CHECK_FALSE(job["params"]["floors"][0].contains("parts"));
    CHECK_FALSE(job["params"]["floors"][0].contains("strip_lights"));
    CHECK_FALSE(job["params"].contains("site_parts"));
}

TEST_CASE("the test box becomes a part with its slots, collision and light", "[core][parts][schema]") {
    plynth::test::TempDir tmp("parts-box");
    const BoxOn on;
    const Project project = boxProject();
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());

    REQUIRE(plan.floorParts.size() == 1);
    REQUIRE(plan.floorParts[0].size() == 1);
    const BuildPart& part = plan.floorParts[0][0];
    CHECK(part.object == "f1_TestBox");
    CHECK(part.kind == "test_box");
    CHECK(part.id == "f1");
    REQUIRE(part.prisms.size() == 1);
    CHECK(part.prisms[0].slot == "Deck");
    CHECK(part.prisms[0].sideSlot == "Glass");
    REQUIRE(part.collision.size() == 1);
    CHECK(part.collision[0].top == Approx(1.0));
    CHECK(hasSlot(plan, "Deck", "oak_planks", "Deck"));
    CHECK(hasSlot(plan, "Glass", "tile", "Glass"));

    REQUIRE(plan.siteParts.size() == 1);
    CHECK(plan.siteParts[0].faces[0].slot == "Light_Strip");
    CHECK(hasSlot(plan, "Light_Strip", "", "Light_Strip"));

    REQUIRE(plan.stripLights[0].size() == 1);
    CHECK(plan.stripLights[0][0].facing == Vec3{0.0, 0.0, -1.0});

    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    const Json& floor = job["params"]["floors"][0];
    REQUIRE(floor["parts"].size() == 1);
    CHECK(floor["parts"][0]["object"] == "f1_TestBox");
    CHECK(floor["parts"][0]["plynth_kind"] == "test_box");
    CHECK(floor["parts"][0]["uv"] == "world");
    CHECK(floor["parts"][0]["prisms"][0]["side_slot"] == "Glass");
    CHECK(floor["parts"][0]["collision"][0]["bottom"] == 0.0);
    REQUIRE(floor["strip_lights"].size() == 1);
    CHECK(floor["strip_lights"][0]["id"] == "ls1");
    REQUIRE(job["params"]["site_parts"].size() == 1);
    CHECK(job["params"]["site_parts"][0]["uv"] == "along");
    CHECK(job["params"]["site_parts"][0]["prisms"].empty());
    bool slotListed = false;
    for (const Json& slot : job["params"]["slots"]) slotListed = slotListed || slot["name"] == "Light_Strip";
    CHECK(slotListed);

    const std::string partsProblem = schemaProblem("blender-job-parts.schema.json", partsOf(job));
    INFO(partsProblem);
    CHECK(partsProblem.empty());
    const std::string jobProblem = schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump()));
    INFO(jobProblem);
    CHECK(jobProblem.empty());
}

TEST_CASE("a part name that shows two materials gets the material added", "[core][parts]") {
    BoxSettings settings;
    settings.sideDeck = true;
    const BoxOn on(settings);
    const BuildPlan plan = makeBuildPlan(boxProject(), sampleAssetSource(), sampleMaterialSource());
    REQUIRE(plan.floorParts[0].size() == 1);
    CHECK(plan.floorParts[0][0].prisms[0].slot == "Deck_OakPlanks");
    CHECK(plan.floorParts[0][0].prisms[0].sideSlot == "Deck_Tile");
    CHECK(hasSlot(plan, "Deck_OakPlanks", "oak_planks", "Deck"));
    CHECK(hasSlot(plan, "Deck_Tile", "tile", "Deck"));
}

TEST_CASE("a part material the library does not have gets the neutral look and a problem", "[core][parts]") {
    BoxSettings settings;
    settings.deckMaterial = "no_such_material";
    const BoxOn on(settings);
    const BuildPlan plan = makeBuildPlan(boxProject(), sampleAssetSource(), sampleMaterialSource());
    CHECK(plan.floorParts[0][0].prisms[0].slot == "Deck");
    CHECK(hasSlot(plan, "Deck", "", "Deck"));
    const auto found = std::find_if(plan.problems.begin(), plan.problems.end(), [](const BuildProblem& p) {
        return p.code == "missing_material" && p.object == "f1";
    });
    REQUIRE(found != plan.problems.end());
    CHECK(found->floor == "f1");
}

TEST_CASE("a part that breaks the job's rules is left out with part_invalid", "[core][parts]") {
    BoxSettings settings;
    settings.bad = true;
    const BoxOn on(settings);
    const BuildPlan plan = makeBuildPlan(boxProject(), sampleAssetSource(), sampleMaterialSource());
    REQUIRE(plan.floorParts[0].size() == 1);
    const auto invalid = std::count_if(plan.problems.begin(), plan.problems.end(), [](const BuildProblem& p) {
        return p.code == "part_invalid" && p.severity == BuildSeverity::Error && p.object == "f1" && p.floor == "f1";
    });
    CHECK(invalid == 2);
}

TEST_CASE("the surfaces group takes the kinds of its part makers", "[core][parts][exportgroups]") {
    const Project project = boxProject();
    const auto holds = [](const std::vector<ExportGroup>& groups, const std::string& id) {
        for (const ExportGroup& g : groups) {
            if (g.id != id) continue;
            return std::find(g.members.begin(), g.members.end(), ExportMember{"test_box", ""}) != g.members.end();
        }
        return false;
    };
    CHECK_FALSE(holds(automaticExportGroups(project), "surfaces"));
    const BoxOn on;
    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    CHECK(holds(groups, "surfaces"));
    CHECK_FALSE(holds(groups, "structure"));
}

TEST_CASE("a roof cut is made in the roof faces and their collision", "[core][parts][roofs]") {
    const Project project = boxProject();
    const BuildPlan plain = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    REQUIRE_FALSE(plain.floors[0].roofs.empty());

    BoxSettings settings;
    settings.cut = true;
    const BoxOn on(settings);
    const BuildPlan cut = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    REQUIRE(cut.roofCuts.size() == 1);
    CHECK(coveringArea(plain.floors[0]) - coveringArea(cut.floors[0]) == Approx(1.0).margin(1e-6));
    bool holed = false;
    for (const RoofFace& face : cut.floors[0].roofs.front().faces) holed = holed || !face.holes.empty();
    CHECK(holed);
    CHECK(cut.collision[0] != plain.collision[0]);
}

TEST_CASE("cutRoofFaces cuts a sloped face, an upright face and a face that looks down", "[core][parts][roofs]") {
    PartPrism through;
    through.outline = square(1.0, 1.0, 2.0, 2.0);
    through.bottom = -10.0;
    through.top = 10.0;

    RoofFace slope;
    slope.outline = {{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, {4.0, 4.0, 2.0}, {0.0, 4.0, 2.0}};
    std::vector<RoofFace> faces{slope};
    cutRoofFaces(faces, through);
    REQUIRE(faces.size() == 1);
    REQUIRE(faces[0].holes.size() == 1);
    CHECK(faceAreaFromAbove(faces[0]) == Approx(15.0));
    for (const Vec3& p : faces[0].holes[0]) CHECK(p.z == Approx(p.y / 2.0).margin(1e-9));

    PartPrism band;
    band.outline = square(1.0, 0.5, 2.0, 3.0);
    band.bottom = 0.0;
    band.top = 1.0;
    faces = {slope};
    cutRoofFaces(faces, band);
    double area = 0.0;
    for (const RoofFace& f : faces) area += faceAreaFromAbove(f);
    CHECK(area == Approx(16.0 - 1.5));

    RoofFace under;
    under.part = RoofPart::Underside;
    under.outline = {{0.0, 0.0, 0.0}, {0.0, 4.0, 0.0}, {4.0, 4.0, 0.0}, {4.0, 0.0, 0.0}};
    faces = {under};
    cutRoofFaces(faces, through);
    REQUIRE(faces.size() == 1);
    CHECK(faces[0].part == RoofPart::Underside);
    CHECK(faceAreaFromAbove(faces[0]) == Approx(-15.0));

    RoofFace upright;
    upright.part = RoofPart::Gable;
    upright.outline = {{0.0, 0.0, 0.0}, {0.0, 4.0, 0.0}, {0.0, 4.0, 2.0}, {0.0, 0.0, 2.0}};
    PartPrism across;
    across.outline = square(-0.5, 1.0, 0.5, 2.0);
    across.bottom = 0.5;
    across.top = 1.5;
    faces = {upright};
    cutRoofFaces(faces, across);
    REQUIRE(faces.size() == 1);
    REQUIRE(faces[0].holes.size() == 1);
    CHECK(areaFromPlusX(faces[0]) == Approx(7.0));
    for (const Vec3& p : faces[0].outline) CHECK(p.x == Approx(0.0).margin(1e-9));

    PartPrism away;
    away.outline = square(5.0, 5.0, 6.0, 6.0);
    away.bottom = -10.0;
    away.top = 10.0;
    faces = {slope, upright};
    cutRoofFaces(faces, away);
    CHECK(faces == std::vector<RoofFace>{slope, upright});

    PartPrism all;
    all.outline = square(-1.0, -1.0, 5.0, 5.0);
    all.bottom = -10.0;
    all.top = 10.0;
    faces = {slope};
    cutRoofFaces(faces, all);
    CHECK(faces.empty());
}

TEST_CASE("buildPartToJson and stripLightToJson meet the parts schema", "[core][parts][schema]") {
    BuildPart part;
    part.object = "f1_dk1_Deck";
    part.kind = "deck";
    part.id = "dk1";
    PartPrism prism;
    prism.outline = square(0.0, 0.0, 1.0, 1.0);
    prism.holes = {{{0.2, 0.2}, {0.2, 0.8}, {0.8, 0.8}, {0.8, 0.2}}};
    prism.top = 0.2;
    prism.slot = "Deck";
    part.prisms.push_back(prism);
    PartSweep sweep;
    sweep.sweep.sections = {{{0, 0, 1}, {0.1, 0, 1}, {0.1, 0.1, 1}}, {{0, 1, 1}, {0.1, 1, 1}, {0.1, 1.1, 1}}};
    sweep.slot = "Railing";
    part.sweeps.push_back(sweep);
    StripLight light;
    light.id = "ls1";
    light.facing = {0.0, 0.0, -1.0};

    const Json json = buildPartToJson(part);
    CHECK(json["prisms"][0]["side_slot"].is_null());
    CHECK(json["prisms"][0]["holes"].size() == 1);
    CHECK(json["sweeps"][0]["closed"] == false);
    CHECK(json["bevel"] == true);
    CHECK(json["collision"].empty());
    Json all = Json::object();
    all["parts"] = Json::array({json});
    all["strip_lights"] = Json::array({stripLightToJson(light)});
    const std::string problem = schemaProblem("blender-job-parts.schema.json", nlohmann::json::parse(all.dump()));
    INFO(problem);
    CHECK(problem.empty());
}

TEST_CASE("the preview parts read a site part", "[core][parts][preview]") {
    plynth::test::TempDir tmp("parts-preview");
    const fs::path folder = tmp.path() / "preview";
    fs::create_directories(folder);
    for (const char* name : {"f1.fbx", "site.fbx"}) std::ofstream(folder / name) << "fbx";
    const nlohmann::json parts = {{"format", "plynth-build-preview"},
                                  {"version", 1},
                                  {"parts", {{{"floor", "f1"}, {"kind", "storey"}, {"file", "f1.fbx"}},
                                             {{"floor", "f1"}, {"kind", "site"}, {"file", std::string(kPreviewSiteFileName)}}}}};
    const std::string problem = schemaProblem("build-preview.schema.json", parts);
    INFO(problem);
    CHECK(problem.empty());
    std::ofstream(folder / "parts.json") << parts.dump();

    const PreviewPartsRead read = readPreviewParts(tmp.path());
    requireOk(read.status);
    REQUIRE(read.parts.size() == 2);
    CHECK(read.parts[1].kind == PreviewPartKind::Site);
    CHECK(read.parts[1].floor == "f1");
    CHECK(previewPartKindName(PreviewPartKind::Site) == "site");
    CHECK(previewPartKindFromName("site") == PreviewPartKind::Site);
}
