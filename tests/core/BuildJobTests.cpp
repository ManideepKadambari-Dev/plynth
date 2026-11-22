#include "BuildFixtures.h"
#include "MaterialFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Materials.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// The path of a build test data file.
fs::path dataFile(const std::string& name)
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / "buildjob" / name;
}

// A data file as ordered JSON.
Json readData(const std::string& name)
{
    return Json::parse(plynth::test::readTextFile(dataFile(name)));
}

// The codes of a list of problems.
std::vector<std::string> codes(const std::vector<BuildProblem>& problems)
{
    std::vector<std::string> out;
    for (const BuildProblem& p : problems) out.push_back(p.code);
    return out;
}

// Checks a job against the job schema.
std::string jobProblem(const Json& job)
{
    return plynth::test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump()));
}

}

TEST_CASE("makeBuildPlan leaves out what cannot be built and says why", "[core][build]") {
    const Project project = buildProject();
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource());

    REQUIRE(plan.floors.size() == 1);
    const FloorBuild& floor = plan.floors[0];
    REQUIRE(floor.id == "f1");
    REQUIRE(floor.wallSolids.size() == 1);
    REQUIRE(floor.openings.size() == 1);
    REQUIRE(floor.openings[0].id == "o1");
    REQUIRE(floor.rooms.size() == 1);
    REQUIRE(floor.rooms[0].id == "r1");
    REQUIRE(floor.items.size() == 2);
    REQUIRE(floor.items[0].id == "i1");
    REQUIRE(floor.items[1].id == "i4");

    REQUIRE(plan.assets.size() == 1);
    REQUIRE(plan.assets[0].id == "sofa");
    REQUIRE(plan.assets[0].blendFile == fs::path("D:/Library/assets/items/sofa/asset.blend"));

    REQUIRE(codes(plan.problems) == std::vector<std::string>{"opening_does_not_fit", "room_not_closed", "overlaps_item",
                                                              "missing_asset", "asset_broken", "overlaps_item"});
    const std::vector<std::string> objects{"o2", "r2", "i1", "i2", "i3", "i4"};
    for (std::size_t i = 0; i < plan.problems.size(); ++i) {
        REQUIRE(plan.problems[i].object == objects[i]);
        REQUIRE(plan.problems[i].floor == "f1");
        REQUIRE(plan.problems[i].step == kCheckStep);
        REQUIRE_FALSE(plan.problems[i].message.empty());
    }
    REQUIRE(plan.problems[0].severity == BuildSeverity::Error);
    REQUIRE(plan.problems[1].severity == BuildSeverity::Error);
    REQUIRE(plan.problems[2].severity == BuildSeverity::Warning);
    REQUIRE(plan.problems[3].severity == BuildSeverity::Error);
    REQUIRE(plan.problems[4].severity == BuildSeverity::Error);
    REQUIRE(plan.problems[5].severity == BuildSeverity::Warning);
}

TEST_CASE("makeBuildPlan stacks floors by elevation", "[core][build]") {
    Project project = buildProject();
    Floor upstairs = project.plan.floors[0];
    upstairs.id = "f2";
    upstairs.openings.clear();
    upstairs.rooms.resize(1);
    upstairs.items.clear();
    project.plan.floors.push_back(upstairs);
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource());
    REQUIRE(plan.floors.size() == 2);
    REQUIRE(plan.floors[0].elevation == 0.0);
    REQUIRE(plan.floors[1].id == "f2");
    REQUIRE(plan.floors[1].elevation == 3.2);
    REQUIRE(plan.floors[1].rooms[0].floorZ == 3.2);
}

TEST_CASE("an empty asset source means no library", "[core][build]") {
    const BuildPlan plan = makeBuildPlan(buildProject(), {});
    REQUIRE(plan.assets.empty());
    REQUIRE(plan.floors[0].items.empty());
    REQUIRE_FALSE(plan.problems.empty());
    REQUIRE(plan.problems[0].code == "no_library");
    REQUIRE(plan.problems[0].severity == BuildSeverity::Error);
    REQUIRE(plan.problems[0].object.empty());
    REQUIRE(plan.problems[0].floor.empty());
    REQUIRE(codes(plan.problems) == std::vector<std::string>{"no_library", "opening_does_not_fit", "room_not_closed"});
}

TEST_CASE("checkBuildable needs a wall somewhere", "[core][build]") {
    const Status empty = checkBuildable(makeEmptyPlan());
    REQUIRE(empty.code == ErrorCode::InvalidValue);
    REQUIRE(empty.message == "The plan has no walls to build.");
    plynth::test::requireOk(checkBuildable(buildProject().plan));
}

TEST_CASE("severity names", "[core][build]") {
    REQUIRE(buildSeverityName(BuildSeverity::Error) == "error");
    REQUIRE(buildSeverityName(BuildSeverity::Warning) == "warning");
    REQUIRE(buildSeverityFromName("warning") == BuildSeverity::Warning);
    REQUIRE_FALSE(buildSeverityFromName("fatal"));
}

TEST_CASE("buildProblemToJson uses null for an empty floor or object", "[core][build]") {
    BuildProblem p;
    p.code = "no_library";
    p.severity = BuildSeverity::Error;
    p.message = "No library.";
    p.step = "check";
    const Json json = buildProblemToJson(p);
    REQUIRE(json["floor"].is_null());
    REQUIRE(json["object"].is_null());
    REQUIRE(json["severity"] == "error");
    p.floor = "f1";
    p.object = "i2";
    const Json full = buildProblemToJson(p);
    REQUIRE(full["floor"] == "f1");
    REQUIRE(full["object"] == "i2");
}

TEST_CASE("makeBuildJob validates against the job schema", "[core][build][schema]") {
    plynth::test::TempDir tmp("build-job");
    const Project project = buildProject();
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource());
    BuildSettings settings;
    const Json job = makeBuildJob(project, plan, settings, tmp.path(), "build-1a2b3c4d", tmp.path() / "build" / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    REQUIRE(job["task"] == "build");
    const Json& params = job["params"];
    REQUIRE(params["plan"] == planToJson(project.plan));
    REQUIRE(params["fbx_file"] == "building.fbx");
    REQUIRE(params["blend_file"] == "building.blend");
    REQUIRE(params["output_folder"] == pathToUtf8(tmp.path() / "build"));
    REQUIRE(params["project"]["folder"] == pathToUtf8(tmp.path()));
    REQUIRE(params["settings"]["bevel_width"] == kBuildBevelWidth);
    REQUIRE(params["settings"]["bevel_segments"] == kBuildBevelSegments);
    REQUIRE(params["settings"]["fill_openings"] == true);
    REQUIRE(params["assets"].size() == 1);
    REQUIRE(params["assets"][0]["collection"] == "sofa");
    REQUIRE(params["floors"].size() == 1);
    REQUIRE(params["floors"][0]["items"].size() == 2);
    REQUIRE(params["floors"][0]["openings"].size() == 1);
    REQUIRE(params["floors"][0]["wall_solids"][0]["holes"].size() == 1);

    settings.writeFbx = false;
    settings.bevelWidth = 0.0;
    const Json noFbx = makeBuildJob(project, plan, settings, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(noFbx["params"]["fbx_file"].is_null());
    REQUIRE(jobProblem(noFbx).empty());
}

TEST_CASE("makeBuildJob validates for a curved wall and a round arch window", "[core][build][schema]") {
    plynth::test::TempDir tmp("build-job-curved");
    Project project = buildProject();
    Floor& f = project.plan.floors[0];
    findWall(f, "w3")->bulge = 0.8;
    findPoint(f, "p2")->cornerRadius = 0.5;
    Opening arch;
    arch.id = "o5";
    arch.wall = "w4";
    arch.offset = 2.0;
    arch.type = OpeningType::Window;
    arch.shape = "round_arch";
    arch.width = 1.0;
    arch.height = 1.4;
    arch.sill = 0.9;
    f.openings.push_back(arch);
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource());
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
}

TEST_CASE("readBuildResult reads a valid result", "[core][build]") {
    const Json result = readData("result-valid.json");
    REQUIRE(plynth::test::schemaProblem("blender-result.schema.json", nlohmann::json::parse(result.dump())).empty());
    const BuildWorkerOutput out = readBuildResult(result, "build-00000001");
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.header.blenderVersion == "5.2.0");
    REQUIRE(out.blendFile == "building.blend");
    REQUIRE(out.fbxFile == "building.fbx");
    REQUIRE(out.objects == 42);
    REQUIRE(out.triangles == 18200);
    REQUIRE(out.counts == BuildCounts{1, 9, 7, 5, 4, 6, 2, 1, 3, 31});
    REQUIRE(out.steps.size() == 3);
    REQUIRE(out.steps[2].step == "openings");
    REQUIRE(out.steps[2].ms == 3400);
    REQUIRE(out.problems.size() == 2);
    REQUIRE(out.problems[0].code == "cut_failed");
    REQUIRE(out.problems[0].severity == BuildSeverity::Error);
    REQUIRE(out.problems[0].floor == "f1");
    REQUIRE(out.problems[0].object == "o3");
    REQUIRE(out.problems[0].step == "openings");
    REQUIRE(out.problems[1].severity == BuildSeverity::Warning);
    REQUIRE(out.problems[1].floor.empty());
    REQUIRE(out.problems[1].object.empty());
}

TEST_CASE("readBuildResult with a null fbx_file", "[core][build]") {
    Json result = readData("result-valid.json");
    result["output"]["fbx_file"] = nullptr;
    const BuildWorkerOutput out = readBuildResult(result, "build-00000001");
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.fbxFile.empty());
}

TEST_CASE("readBuildResult refuses a result with no counts or a bad severity", "[core][build]") {
    const BuildWorkerOutput noCounts = readBuildResult(readData("result-missing-counts.json"), "build-00000001");
    REQUIRE(noCounts.header.status.code == ErrorCode::BadFile);
    REQUIRE(noCounts.header.status.where == "/output/counts");

    const BuildWorkerOutput fatal = readBuildResult(readData("result-fatal.json"), "build-00000001");
    REQUIRE(fatal.header.status.code == ErrorCode::BadFile);
    REQUIRE(fatal.header.status.where == "/output/problems/0/severity");
    REQUIRE(plynth::test::schemaProblem("blender-result.schema.json", nlohmann::json::parse(readData("result-fatal.json").dump())).size() > 0);
}

TEST_CASE("readBuildResult of a failed job is blender_failed with the code", "[core][build]") {
    const BuildWorkerOutput out = readBuildResult(readData("result-failed.json"), "build-00000001");
    REQUIRE(out.header.status.code == ErrorCode::BlenderFailed);
    REQUIRE(out.header.errorCode == "save_failed");
    REQUIRE(out.header.status.message == "The blend file could not be saved.");
}

TEST_CASE("readBuildResult refuses another job id", "[core][build]") {
    REQUIRE(readBuildResult(readData("result-valid.json"), "build-other").header.status.code == ErrorCode::BadFile);
}

namespace {

// The plan with a door and a window of the materials tests: the split room, a window on w3, tile in r2, and
// the broken material plaster on the outside of w5.
Project materialsProject()
{
    Project project = splitRoomProject();
    Floor& f = project.plan.floors[0];
    Opening window;
    window.id = "o2";
    window.wall = "w3";
    window.offset = 2.0;
    window.type = OpeningType::Window;
    window.width = 1.2;
    window.height = 1.2;
    window.sill = 0.9;
    f.openings.push_back(window);
    findRoom(f, "r2")->floorMaterial = "tile";
    findRoom(f, "r2")->wallMaterial = "tile";
    findWall(f, "w5")->materialOutside = "plaster";
    return project;
}

// The id of a material when the sample source can use it, otherwise null.
Json usableOrNull(const std::optional<std::string>& id)
{
    if (!id) return nullptr;
    const std::optional<BuildMaterialState> state = sampleMaterialSource()(*id);
    return state && !state->broken ? Json(*id) : Json(nullptr);
}

// Every ring of every wall solid of the first floor of a job.
std::vector<Json> rings(const Json& job)
{
    std::vector<Json> out;
    for (const Json& solid : job["params"]["floors"][0]["wall_solids"]) {
        out.push_back(solid["outline"]);
        for (const Json& hole : solid["holes"]) out.push_back(hole);
    }
    return out;
}

}

TEST_CASE("makeBuildPlan lists each usable material once and reports the others", "[core][build][materials]") {
    const Project project = materialsProject();
    const BuildPlan plan = makeBuildPlan(project, {}, sampleMaterialSource());

    std::vector<std::string> ids;
    for (const BuildMaterial& m : plan.materials) ids.push_back(m.id);
    REQUIRE(ids == std::vector<std::string>{"brushed_steel", "clear_glass", "concrete", "oak_planks", "painted_wood", "tile", "white_paint"});
    REQUIRE(plan.materials[1].tileSize == TileSize{2.0, 1.0});
    REQUIRE(plan.materials[1].baseColor == fs::path("D:/Library/materials/items/clear_glass/textures/base_color.png"));
    REQUIRE_FALSE(plan.materials[1].opacity.empty());
    REQUIRE(plan.materials[0].opacity.empty());

    REQUIRE(plan.problems.size() == 2);
    REQUIRE(plan.problems[0].code == "material_broken");
    REQUIRE(plan.problems[0].severity == BuildSeverity::Warning);
    REQUIRE(plan.problems[0].object == "w5");
    REQUIRE(plan.problems[0].floor == "f1");
    REQUIRE(plan.problems[0].step == kCheckStep);
    REQUIRE(plan.problems[0].message.find("plaster") != std::string::npos);
    REQUIRE(plan.problems[0].message.find("The base color map is missing.") != std::string::npos);
    REQUIRE(plan.problems[1].code == "missing_material");
    REQUIRE(plan.problems[1].object.empty());
    REQUIRE(plan.problems[1].floor.empty());
    REQUIRE(plan.problems[1].message.find("red_brick") != std::string::npos);
    REQUIRE(plan.problems[1].message.find("wall_outside") != std::string::npos);
}

TEST_CASE("the job ring edges follow the material rules", "[core][build][materials]") {
    plynth::test::TempDir tmp("build-job-materials");
    const Project project = materialsProject();
    const Floor& floor = project.plan.floors[0];
    const BuildPlan plan = makeBuildPlan(project, {}, sampleMaterialSource());
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());

    int inside = 0;
    int outside = 0;
    for (const Json& ring : rings(job)) {
        for (const Json& edge : ring["edges"]) {
            if (edge["side"].is_null()) {
                REQUIRE(edge["material"] == "white_paint");
                continue;
            }
            const Wall* wall = findWall(floor, edge["wall"].get<std::string>());
            const Room* room = edge["room"].is_null() ? nullptr : findRoom(floor, edge["room"].get<std::string>());
            REQUIRE(edge["material"] == usableOrNull(wallFaceMaterial(project, *wall, room).material));
            if (room) {
                ++inside;
                REQUIRE(edge["material"] == (room->id == "r2" ? "tile" : "white_paint"));
            } else {
                ++outside;
                REQUIRE(edge["material"].is_null());
            }
        }
    }
    REQUIRE(inside > 0);
    REQUIRE(outside > 0);
    REQUIRE(job["params"]["floors"][0]["wall_solids"][0]["top_material"] == "white_paint");
}

TEST_CASE("the job opening and room materials follow the material rules", "[core][build][materials]") {
    plynth::test::TempDir tmp("build-job-materials-openings");
    const Project project = materialsProject();
    const Floor& floor = project.plan.floors[0];
    const BuildPlan plan = makeBuildPlan(project, {}, sampleMaterialSource());
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    const Json& floorJson = job["params"]["floors"][0];

    REQUIRE(floorJson["openings"].size() == 2);
    const Json& door = floorJson["openings"][0]["materials"];
    const OpeningMaterials doorRules = openingMaterials(project, floor, floor.openings[0]);
    REQUIRE(door["reveal"] == usableOrNull(doorRules.reveal.material));
    REQUIRE(door["threshold"] == usableOrNull(doorRules.threshold.material));
    REQUIRE(door["threshold"] == "oak_planks");
    REQUIRE(door["door"] == "painted_wood");
    REQUIRE(door["handle"] == "brushed_steel");
    REQUIRE(door["frame"] == "white_paint");
    REQUIRE(door["glass"].is_null());

    const Json& window = floorJson["openings"][1]["materials"];
    const OpeningMaterials windowRules = openingMaterials(project, floor, floor.openings[1]);
    REQUIRE(window["reveal"] == usableOrNull(windowRules.reveal.material));
    REQUIRE(window["reveal"] == "tile");
    REQUIRE(window["glass"] == "clear_glass");
    REQUIRE(window["frame"] == "white_paint");
    REQUIRE(window["threshold"].is_null());
    REQUIRE(window["door"].is_null());
    REQUIRE(window["handle"].is_null());

    REQUIRE(floorJson["rooms"].size() == 2);
    REQUIRE(floorJson["rooms"][0]["id"] == "r1");
    REQUIRE(floorJson["rooms"][0]["floor_material"] == "oak_planks");
    REQUIRE(floorJson["rooms"][0]["ceiling_material"] == "white_paint");
    REQUIRE(floorJson["rooms"][1]["floor_material"] == "tile");
    REQUIRE(floorJson["rooms"][1]["ceiling_material"] == "white_paint");
}

TEST_CASE("an empty materials source gives the neutral look everywhere and one no_library", "[core][build][materials]") {
    plynth::test::TempDir tmp("build-job-materials-none");
    const Project project = materialsProject();
    const BuildPlan plan = makeBuildPlan(project, {}, {});
    REQUIRE(plan.materials.empty());
    REQUIRE(codes(plan.problems) == std::vector<std::string>{"no_library"});
    REQUIRE(plan.problems[0].object.empty());

    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(jobProblem(job).empty());
    REQUIRE(job["params"]["materials"].empty());
    for (const Json& ring : rings(job)) {
        for (const Json& edge : ring["edges"]) REQUIRE(edge["material"].is_null());
    }
    const Json& floorJson = job["params"]["floors"][0];
    REQUIRE(floorJson["wall_solids"][0]["top_material"].is_null());
    REQUIRE(floorJson["rooms"][0]["floor_material"].is_null());
    REQUIRE(floorJson["openings"][0]["materials"]["door"].is_null());

    Project bare = splitRoomProject();
    clearMaterialDefaults(bare);
    REQUIRE(makeBuildPlan(bare, {}, {}).problems.empty());

    Project withItems = buildProject();
    withItems.settings.materials = MaterialDefaults{};
    const std::vector<std::string> both = codes(makeBuildPlan(withItems, {}, {}).problems);
    REQUIRE(std::count(both.begin(), both.end(), "no_library") == 1);
}

TEST_CASE("the job lists map paths as absolute with forward slashes and a null materials_file without an fbx", "[core][build][materials]") {
    plynth::test::TempDir tmp("build-job-materials-paths");
    const Project project = materialsProject();
    const BuildMaterialSource relative = [](std::string_view id) -> std::optional<BuildMaterialState> {
        std::optional<BuildMaterialState> state = sampleMaterialSource()(id);
        if (state && !state->broken) {
            state->material.baseColor = fs::path("lib") / std::string(id) / "base_color.png";
            state->material.normal = fs::path("lib") / std::string(id) / "normal.png";
        }
        return state;
    };
    const BuildPlan plan = makeBuildPlan(project, {}, relative);
    BuildSettings settings;
    const Json job = makeBuildJob(project, plan, settings, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(jobProblem(job).empty());
    const Json& params = job["params"];
    REQUIRE(params["materials_file"] == "materials.json");
    REQUIRE(params["textures_folder"] == "textures");
    REQUIRE(params["materials"].size() == 7);
    for (const Json& m : params["materials"]) {
        for (const auto& [channel, value] : m["maps"].items()) {
            if (value.is_null()) continue;
            const std::string path = value.get<std::string>();
            INFO(channel << " " << path);
            REQUIRE(fs::path(path).is_absolute());
            REQUIRE(path.find('\\') == std::string::npos);
        }
        REQUIRE(m["maps"]["opacity"].is_null() == (m["id"] != "clear_glass"));
        REQUIRE(m["maps"]["occlusion"].is_null());
    }

    settings.writeFbx = false;
    const Json noFbx = makeBuildJob(project, plan, settings, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(jobProblem(noFbx).empty());
    REQUIRE(noFbx["params"]["fbx_file"].is_null());
    REQUIRE(noFbx["params"]["materials_file"].is_null());
}

TEST_CASE("readBuildResult reads materials_file and counts.materials", "[core][build][materials]") {
    const BuildWorkerOutput out = readBuildResult(readData("result-valid.json"), "build-00000001");
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.materialsFile == "materials.json");
    REQUIRE(out.counts.materials == 6);
    REQUIRE(out.counts == BuildCounts{1, 9, 7, 5, 4, 6, 2, 1, 3, 31});

    Json noFbx = readData("result-valid.json");
    noFbx["output"]["fbx_file"] = nullptr;
    noFbx["output"]["materials_file"] = nullptr;
    const BuildWorkerOutput none = readBuildResult(noFbx, "build-00000001");
    plynth::test::requireOk(none.header.status);
    REQUIRE(none.materialsFile.empty());

    Json missing = readData("result-valid.json");
    missing["output"]["counts"].erase("materials");
    const BuildWorkerOutput bad = readBuildResult(missing, "build-00000001");
    REQUIRE(bad.header.status.code == ErrorCode::BadFile);
    REQUIRE(bad.header.status.where == "/output/counts/materials");
    REQUIRE(plynth::test::schemaProblem("blender-result.schema.json", nlohmann::json::parse(missing.dump())).size() > 0);
}

TEST_CASE("readBuildResult needs the slab, stair and roof counts", "[core][build]") {
    for (const char* key : {"slabs", "stairs", "roofs"}) {
        INFO(key);
        Json result = readData("result-valid.json");
        result["output"]["counts"].erase(key);
        const BuildWorkerOutput out = readBuildResult(result, "build-00000001");
        REQUIRE(out.header.status.code == ErrorCode::BadFile);
        REQUIRE(out.header.status.where == std::string("/output/counts/") + key);
        REQUIRE_FALSE(plynth::test::schemaProblem("blender-result.schema.json", nlohmann::json::parse(result.dump())).empty());
    }
}

TEST_CASE("the job carries the preview folder, slab, stairs, roofs and ceiling polygons", "[core][build]") {
    plynth::test::TempDir tmp("job-m12");
    const Project project = buildProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(jobProblem(job).empty());
    REQUIRE(job["params"]["preview_folder"] == "preview");
    const Json& floor = job["params"]["floors"][0];
    REQUIRE(floor["slab"]["polygons"].is_array());
    REQUIRE(floor["slab"]["undersides"].empty());
    REQUIRE(floor["slab"]["materials"]["edge"].is_null());
    REQUIRE(floor["stairs"].empty());
    REQUIRE(floor["roofs"].size() == 1);
    REQUIRE(floor["roofs"][0]["faces"].size() >= 2);
    const Json& room = floor["rooms"][0];
    REQUIRE_FALSE(room["polygons"].empty());
    REQUIRE(room["ceiling_polygons"] == room["polygons"]);

    BuildSettings noFbx;
    noFbx.writeFbx = false;
    const Json bare = makeBuildJob(project, plan, noFbx, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(jobProblem(bare).empty());
    REQUIRE(bare["params"]["preview_folder"].is_null());
}

namespace {

// The area of a world polygon with holes.
double worldArea(const WorldPolygon& p)
{
    double a = signedArea(p.outline);
    for (const std::vector<Vec2>& h : p.holes) a += signedArea(h);
    return a;
}

double worldArea(const std::vector<WorldPolygon>& list)
{
    double a = 0.0;
    for (const WorldPolygon& p : list) a += worldArea(p);
    return a;
}

// Two floors of the square room r1, ceilings 3.0 and slabs 0.2, a U shaped stair on f1 inside the room.
Project stairProject()
{
    Project project = buildProject();
    Floor& f1 = project.plan.floors[0];
    f1.openings.clear();
    f1.items.clear();
    f1.rooms.resize(1);
    Floor upstairs = f1;
    upstairs.id = "f2";
    upstairs.name = "Floor 2";
    for (Point& p : upstairs.points) p.id += "u";
    for (Wall& w : upstairs.walls) {
        w.id += "u";
        w.from += "u";
        w.to += "u";
    }
    for (Room& r : upstairs.rooms) {
        r.id += "u";
        for (Id& w : r.walls) w += "u";
    }
    project.plan.floors.push_back(upstairs);
    Stair stair;
    stair.id = "s1";
    stair.type = "u_shape";
    stair.x = 2.0;
    stair.y = 0.5;
    project.plan.floors[0].stairs.push_back(stair);
    return project;
}

}

TEST_CASE("the build plan has a stair, its holes and the slab on a two floor plan", "[core][build][stairs]") {
    const Project project = stairProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    REQUIRE(plan.floors.size() == 2);
    const FloorBuild& f1 = plan.floors[0];
    const FloorBuild& f2 = plan.floors[1];
    REQUIRE(f2.elevation == 3.2);

    REQUIRE(f1.stairs.size() == 1);
    REQUIRE(f1.stairs[0].riserCount == 17);
    REQUIRE(f1.stairs[0].type == "u_shape");
    REQUIRE(f1.stairs[0].toFloor == "f2");
    REQUIRE(f1.stairs[0].steps.front().bottom == 0.0);
    REQUIRE(f2.stairs.empty());
    for (const StairStepBuild& step : f1.stairs[0].steps) REQUIRE(signedArea(step.outline) > 0.0);
    const double holeArea = worldArea(f1.stairs[0].hole);
    REQUIRE(holeArea > 2.0);

    const double whole = worldArea(plan.floors[0].rooms[0].polygons);
    REQUIRE(worldArea(f1.rooms[0].ceilingPolygons) == Approx(whole - holeArea).margin(1e-6));
    REQUIRE(worldArea(f1.rooms[0].polygons) == Approx(whole).margin(1e-6));
    REQUIRE(worldArea(f2.rooms[0].polygons) == Approx(whole - holeArea).margin(1e-6));
    REQUIRE(worldArea(f2.rooms[0].ceilingPolygons) == Approx(whole).margin(1e-6));

    REQUIRE(f1.slab.bottom == 3.0);
    REQUIRE(f1.slab.top == Approx(3.2));
    REQUIRE(f1.slab.polygons.size() == 1);
    REQUIRE(f1.slab.polygons[0].holes.size() == 1);
    REQUIRE(f1.slab.undersides.empty());

    REQUIRE(f1.roofs.empty());
    REQUIRE(f2.roofs.size() == 1);
    REQUIRE(plan.problems.empty());
}

TEST_CASE("a stair on the top floor is an error and is not built", "[core][build][stairs]") {
    Project project = stairProject();
    Stair stair;
    stair.id = "s2";
    stair.x = 2.0;
    stair.y = 0.5;
    project.plan.floors[1].stairs.push_back(stair);
    const BuildPlan plan = makeBuildPlan(project, {});
    REQUIRE(plan.floors[1].stairs.empty());
    REQUIRE(plan.floors[0].stairs.size() == 1);
    bool found = false;
    for (const BuildProblem& p : plan.problems) {
        if (p.code != "stair_no_floor_above") continue;
        found = true;
        REQUIRE(p.severity == BuildSeverity::Error);
        REQUIRE(p.floor == "f2");
        REQUIRE(p.object == "s2");
    }
    REQUIRE(found);
}

TEST_CASE("the roofs follow what lies above each floor", "[core][build][roofs]") {
    Project project = stairProject();
    project.plan.floors[0].stairs.clear();
    REQUIRE(makeBuildPlan(project, {}).floors[0].roofs.empty());

    for (Point& p : project.plan.floors[1].points) p.x -= 2.0;
    const BuildPlan half = makeBuildPlan(project, {});
    REQUIRE(half.floors[0].roofs.size() == 1);
    REQUIRE(half.floors[1].roofs.size() == 1);
}

TEST_CASE("materials of stairs, slabs and roofs follow the rules and a missing one is reported once", "[core][build][materials]") {
    Project project = stairProject();
    project.settings.materials = MaterialDefaults{};
    const BuildPlan plan = makeBuildPlan(project, {}, [](std::string_view id) -> std::optional<BuildMaterialState> {
        if (id == "roof_tiles") return std::nullopt;
        BuildMaterialState state;
        state.material.id = std::string(id);
        state.material.baseColor = "D:/Library/" + std::string(id) + ".png";
        return state;
    });
    REQUIRE(plan.floorMaterials[0].stairs.size() == 1);
    REQUIRE(plan.floorMaterials[0].stairs[0].material == "oak_planks");
    REQUIRE(plan.floorMaterials[0].slab.edge == "red_brick");
    REQUIRE(plan.floorMaterials[0].slab.well == "white_paint");
    REQUIRE(plan.floorMaterials[0].slab.underside.empty()); // no undersides are built, so no material
    REQUIRE(plan.floorMaterials[1].roofs.size() == 1);
    REQUIRE(plan.floorMaterials[1].roofs[0].covering == "concrete");
    REQUIRE(plan.problems.empty());

    project.plan.roof.type = "gable";
    const BuildPlan gable = makeBuildPlan(project, {}, [](std::string_view id) -> std::optional<BuildMaterialState> {
        if (id == "roof_tiles") return std::nullopt;
        BuildMaterialState state;
        state.material.id = std::string(id);
        state.material.baseColor = "D:/Library/" + std::string(id) + ".png";
        return state;
    });
    REQUIRE(gable.floorMaterials[1].roofs[0].covering.empty());
    int missing = 0;
    for (const BuildProblem& p : gable.problems) missing += p.code == "missing_material" ? 1 : 0;
    REQUIRE(missing == 1);
}

TEST_CASE("the job with a stair, slab and roof validates", "[core][build][stairs]") {
    plynth::test::TempDir tmp("job-stairs");
    const Project project = stairProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    const Json& f1 = job["params"]["floors"][0];
    REQUIRE(f1["stairs"].size() == 1);
    REQUIRE(f1["stairs"][0]["riser_count"] == 17);
    REQUIRE(f1["slab"]["polygons"][0]["holes"].size() == 1);
    REQUIRE(f1["rooms"][0]["ceiling_polygons"][0]["holes"].size() == 1);
    REQUIRE(job["params"]["floors"][1]["rooms"][0]["polygons"][0]["holes"].size() == 1);
    REQUIRE(job["params"]["floors"][1]["roofs"].size() == 1);
}

namespace {

// Requires one entry of sides per slab polygon and one name per ring edge, and returns how many are "well".
std::size_t checkSlabSides(const Json& slab)
{
    REQUIRE(slab.contains("sides"));
    REQUIRE(slab.contains("tops"));
    const Json& polygons = slab["polygons"];
    const Json& sides = slab["sides"];
    REQUIRE(sides.size() == polygons.size());
    std::size_t wells = 0;
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        REQUIRE(sides[i]["outline"].size() == polygons[i]["outline"].size());
        REQUIRE(sides[i]["holes"].size() == polygons[i]["holes"].size());
        for (const Json& name : sides[i]["outline"]) wells += name == "well" ? 1 : 0;
        for (std::size_t h = 0; h < polygons[i]["holes"].size(); ++h) {
            REQUIRE(sides[i]["holes"][h].size() == polygons[i]["holes"][h].size());
            for (const Json& name : sides[i]["holes"][h]) wells += name == "well" ? 1 : 0;
        }
    }
    return wells;
}

}

TEST_CASE("the job writes sides for every slab ring and an empty tops list", "[core][build][stairs]") {
    plynth::test::TempDir tmp("job-slab-sides");
    const Project project = stairProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(jobProblem(job).empty());
    const Json& slab = job["params"]["floors"][0]["slab"];
    REQUIRE(slab.contains("sides"));
    REQUIRE(slab.contains("tops"));
    REQUIRE(slab["tops"].empty());
    REQUIRE(checkSlabSides(slab) > 0);
    for (const Json& name : slab["sides"][0]["holes"][0]) REQUIRE(name == "well");

    const Json& top = job["params"]["floors"][1]["slab"];
    REQUIRE(checkSlabSides(top) == 0);
    REQUIRE(top["sides"][0]["holes"].empty());
}

TEST_CASE("a hairline mismatch between floors writes its tops", "[core][build]") {
    plynth::test::TempDir tmp("job-slab-tops");
    Project project = stairProject();
    project.plan.floors[0].stairs.clear();
    for (Point& p : project.plan.floors[1].points) {
        p.x += 0.1;
        p.y += 0.1;
    }
    const BuildPlan plan = makeBuildPlan(project, {});
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    const Json& slab = job["params"]["floors"][0]["slab"];
    checkSlabSides(slab);
    REQUIRE_FALSE(slab["tops"].empty());
    REQUIRE(slab["tops"].size() == plan.floors[0].slab.tops.size());
}

namespace {

// The materials plan with r1 a living room and r2 a bedroom.
Project typedRoomsProject()
{
    Project project = materialsProject();
    Floor& f = project.plan.floors[0];
    findRoom(f, "r1")->type = "living";
    findRoom(f, "r2")->type = "bedroom";
    return project;
}

// The slot table of a job by name.
std::map<std::string, Json> slotTable(const Json& job)
{
    std::map<std::string, Json> out;
    for (const Json& slot : job["params"]["slots"]) out.emplace(slot["name"].get<std::string>(), slot);
    return out;
}

// Makes the job of a project with the sample libraries.
Json jobOf(const Project& project, const std::filesystem::path& folder)
{
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    return makeBuildJob(project, plan, BuildSettings{}, folder, "build-1a2b3c4d", folder / "result.json");
}

}

TEST_CASE("every slot field of the job names one slot with the material beside it", "[core][build][slots]") {
    plynth::test::TempDir tmp("build-job-slots");
    const Json job = jobOf(typedRoomsProject(), tmp.path());
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    const std::map<std::string, Json> table = slotTable(job);

    std::vector<std::string> names;
    for (const Json& slot : job["params"]["slots"]) names.push_back(slot["name"].get<std::string>());
    REQUIRE(std::is_sorted(names.begin(), names.end()));
    REQUIRE(std::adjacent_find(names.begin(), names.end()) == names.end());

    const auto agrees = [&](const Json& slot, const Json& material) {
        REQUIRE(table.count(slot.get<std::string>()) == 1);
        REQUIRE(table.at(slot.get<std::string>())["material"] == material);
    };
    const Json& floorJson = job["params"]["floors"][0];
    for (const Json& ring : rings(job)) {
        for (const Json& edge : ring["edges"]) agrees(edge["slot"], edge["material"]);
    }
    for (const Json& solid : floorJson["wall_solids"]) agrees(solid["top_slot"], solid["top_material"]);
    for (const Json& room : floorJson["rooms"]) {
        agrees(room["floor_slot"], room["floor_material"]);
        agrees(room["ceiling_slot"], room["ceiling_material"]);
    }
    for (const Json& opening : floorJson["openings"]) {
        for (const char* part : {"reveal", "threshold", "door", "frame", "glass", "handle"}) {
            const Json& material = opening["materials"][part];
            if (!material.is_null()) agrees(opening["slots"][part], material);
            else REQUIRE(table.count(opening["slots"][part].get<std::string>()) == 1);
        }
    }
}

TEST_CASE("the job ring edges name their slots by room type", "[core][build][slots]") {
    plynth::test::TempDir tmp("build-job-slots-edges");
    const Json job = jobOf(typedRoomsProject(), tmp.path());
    int living = 0;
    int bedroom = 0;
    int exterior = 0;
    for (const Json& ring : rings(job)) {
        for (const Json& edge : ring["edges"]) {
            if (edge["side"].is_null()) {
                REQUIRE(edge["slot"] == "Wall_Top");
                REQUIRE(edge["material"] == "white_paint");
            } else if (edge["room"].is_null()) {
                REQUIRE(edge["slot"] == "Exterior_Wall");
                ++exterior;
            } else if (edge["room"] == "r1") {
                REQUIRE(edge["slot"] == "Living_Wall");
                ++living;
            } else {
                REQUIRE(edge["slot"] == "Bedroom_Wall");
                ++bedroom;
            }
        }
    }
    REQUIRE(living > 0);
    REQUIRE(bedroom > 0);
    REQUIRE(exterior > 0);
    REQUIRE(job["params"]["floors"][0]["wall_solids"][0]["top_slot"] == "Wall_Top");

    const std::map<std::string, Json> table = slotTable(job);
    REQUIRE(table.at("Living_Wall")["material"] == "white_paint");
    REQUIRE(table.at("Living_Wall")["surface"] == "Wall_Inside");
    REQUIRE(table.at("Bedroom_Wall")["material"] == "tile");
    REQUIRE(table.at("Exterior_Wall")["material"].is_null());
    REQUIRE(table.at("Exterior_Wall")["surface"] == "Wall_Outside");
    REQUIRE(table.at("Wall_Top")["surface"] == "Wall_Top");
}

TEST_CASE("the end faces of a free wall use the wall top slot and material", "[core][build][slots]") {
    plynth::test::TempDir tmp("build-job-slots-ends");
    Project project = makeNewProject();
    Floor f;
    f.id = "f1";
    f.name = "Floor 1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 3.0, 0.0, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    w.thickness = 0.2;
    f.walls.push_back(w);
    project.plan.floors[0] = f;
    const Json job = jobOf(project, tmp.path());
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    int ends = 0;
    for (const Json& ring : rings(job)) {
        for (const Json& edge : ring["edges"]) {
            if (edge["side"].is_null()) {
                ++ends;
                REQUIRE(edge["slot"] == "Wall_Top");
                REQUIRE(edge["slot"] == job["params"]["floors"][0]["wall_solids"][0]["top_slot"]);
                REQUIRE(edge["material"] == job["params"]["floors"][0]["wall_solids"][0]["top_material"]);
            } else {
                REQUIRE(edge["slot"] == "Exterior_Wall");
            }
        }
    }
    REQUIRE(ends == 2);
}

TEST_CASE("the job opening and room slots follow the slot rules", "[core][build][slots]") {
    plynth::test::TempDir tmp("build-job-slots-openings");
    const Project project = typedRoomsProject();
    const Floor& floor = project.plan.floors[0];
    const Json job = jobOf(project, tmp.path());
    const Json& floorJson = job["params"]["floors"][0];

    const OpeningRooms doorRooms = openingRooms(floor, floor.openings[0]);
    REQUIRE(doorRooms.right);
    const Json& door = floorJson["openings"][0]["slots"];
    const std::string rightLabel = roomSlotLabel(*findRoom(floor, *doorRooms.right));
    REQUIRE(door["reveal"] == rightLabel + "_Wall");
    REQUIRE(door["threshold"] == rightLabel + "_Floor");
    REQUIRE(door["door"] == "Door");
    REQUIRE(door["frame"] == "Frame");
    REQUIRE(door["glass"] == "Glass");
    REQUIRE(door["handle"] == "Handle");

    const Json& window = floorJson["openings"][1]["slots"];
    REQUIRE(window["reveal"] == "Bedroom_Wall");
    REQUIRE(window["glass"] == "Glass");
    REQUIRE(window["frame"] == "Frame");

    REQUIRE(floorJson["rooms"][0]["floor_slot"] == "Living_Floor");
    REQUIRE(floorJson["rooms"][0]["ceiling_slot"] == "Living_Ceiling");
    REQUIRE(floorJson["rooms"][1]["floor_slot"] == "Bedroom_Floor");
    REQUIRE(floorJson["rooms"][1]["ceiling_slot"] == "Bedroom_Ceiling");

    const std::map<std::string, Json> table = slotTable(job);
    REQUIRE(table.at("Door")["material"] == "painted_wood");
    REQUIRE(table.at("Door")["surface"] == "Door");
    REQUIRE(table.at("Glass")["material"] == "clear_glass");
    REQUIRE(table.at("Handle")["material"] == "brushed_steel");
    REQUIRE(table.at("Frame")["material"] == "white_paint");
    REQUIRE(table.at("Living_Floor")["surface"] == "Floor");
    REQUIRE(table.at("Bedroom_Ceiling")["surface"] == "Ceiling");
}

TEST_CASE("a threshold with no room on either side uses the Threshold slot", "[core][build][slots]") {
    plynth::test::TempDir tmp("build-job-slots-threshold");
    Project project = typedRoomsProject();
    Floor& f = project.plan.floors[0];
    f.rooms.clear();
    f.openings.erase(f.openings.begin() + 1);
    const Json job = jobOf(project, tmp.path());
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    REQUIRE(job["params"]["floors"][0]["openings"].size() == 1);
    const Json& slots = job["params"]["floors"][0]["openings"][0]["slots"];
    REQUIRE(slots["threshold"] == "Threshold");
    REQUIRE(slots["reveal"] == "Exterior_Wall");
    REQUIRE(slotTable(job).at("Threshold")["surface"] == "Threshold");
}

TEST_CASE("rooms of one type with different floors get the material in the slot name", "[core][build][slots]") {
    plynth::test::TempDir tmp("build-job-slots-clash");
    Project project = typedRoomsProject();
    Floor& f = project.plan.floors[0];
    findRoom(f, "r1")->type = "bedroom";
    findRoom(f, "r1")->floorMaterial = "oak_planks";
    findRoom(f, "r2")->floorMaterial = "tile";
    const Json job = jobOf(project, tmp.path());
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    const Json& rooms = job["params"]["floors"][0]["rooms"];
    REQUIRE(rooms[0]["floor_slot"] == "Bedroom_Floor_OakPlanks");
    REQUIRE(rooms[1]["floor_slot"] == "Bedroom_Floor_Tile");
    const std::map<std::string, Json> table = slotTable(job);
    REQUIRE(table.count("Bedroom_Floor") == 0);
    REQUIRE(table.at("Bedroom_Floor_OakPlanks")["material"] == "oak_planks");
    REQUIRE(table.at("Bedroom_Floor_Tile")["material"] == "tile");
    REQUIRE(rooms[0]["ceiling_slot"] == "Bedroom_Ceiling");
    REQUIRE(rooms[1]["ceiling_slot"] == "Bedroom_Ceiling");
}

TEST_CASE("a missing or broken material counts as the neutral look when slot names are compared", "[core][build][slots]") {
    plynth::test::TempDir tmp("build-job-slots-neutral");
    Project project = typedRoomsProject();
    Floor& f = project.plan.floors[0];
    findRoom(f, "r1")->type = "bedroom";
    findRoom(f, "r1")->floorMaterial = "plaster";
    findRoom(f, "r2")->floorMaterial = "no_such_material";
    Json job = jobOf(project, tmp.path());
    Json rooms = job["params"]["floors"][0]["rooms"];
    REQUIRE(rooms[0]["floor_slot"] == "Bedroom_Floor");
    REQUIRE(rooms[1]["floor_slot"] == "Bedroom_Floor");
    REQUIRE(slotTable(job).at("Bedroom_Floor")["material"].is_null());

    findRoom(f, "r2")->floorMaterial = "oak_planks";
    job = jobOf(project, tmp.path());
    rooms = job["params"]["floors"][0]["rooms"];
    REQUIRE(rooms[0]["floor_slot"] == "Bedroom_Floor_Neutral");
    REQUIRE(rooms[1]["floor_slot"] == "Bedroom_Floor_OakPlanks");
    REQUIRE(jobProblem(job).empty());
}

TEST_CASE("the collision in the job equals floorCollision of every floor", "[core][build][collision]") {
    plynth::test::TempDir tmp("build-job-collision");
    const Project project = typedRoomsProject();
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    REQUIRE(plan.collision.size() == plan.floors.size());
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        REQUIRE(plan.collision[i] == floorCollision(project.plan.floors[i], plan.floors[i]));
    }
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(jobProblem(job).empty());

    const Json& list = job["params"]["floors"][0]["collision"];
    REQUIRE(list.size() == plan.collision[0].size());
    REQUIRE(list.size() > 0);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const CollisionShape& shape = plan.collision[0][i];
        REQUIRE(list[i]["target"]["kind"] == std::string(collisionTargetKindName(shape.kind)));
        REQUIRE(list[i]["target"]["id"] == shape.id);
        REQUIRE(list[i]["pieces"].size() == shape.pieces.size());
        for (std::size_t p = 0; p < shape.pieces.size(); ++p) {
            REQUIRE(list[i]["pieces"][p]["bottom"] == shape.pieces[p].bottom);
            REQUIRE(list[i]["pieces"][p]["top"] == shape.pieces[p].top);
            REQUIRE(list[i]["pieces"][p]["outline"].size() == shape.pieces[p].outline.size());
        }
    }
    REQUIRE(list[0]["target"]["kind"] == "walls");
}

TEST_CASE("the job carries the preview texture settings", "[core][build][preview]") {
    plynth::test::TempDir tmp("build-job-preview");
    const Json job = jobOf(typedRoomsProject(), tmp.path());
    REQUIRE(job["params"]["preview_textures_folder"] == std::string(kBuildPreviewTexturesFolder));
    REQUIRE(job["params"]["preview_texture_size"] == 1024);
}

TEST_CASE("the job lists the maps of every asset material", "[core][build][assets]") {
    plynth::test::TempDir tmp("build-job-asset-materials");
    const Project project = buildProject();
    const BuildAssetSource source = [](std::string_view id) -> std::optional<BuildAssetState> {
        if (id != "sofa") return std::nullopt;
        BuildAssetState state;
        state.blendFile = "D:/Library/assets/items/sofa/asset.blend";
        BuildAssetMaterial fabric;
        fabric.name = "Fabric";
        fabric.baseColor = "D:/Library/assets/items/sofa/textures/maps/fabric/base_color.png";
        fabric.normal = "D:/Library/assets/items/sofa/textures/maps/fabric/normal.png";
        fabric.roughness = "D:/Library/assets/items/sofa/textures/maps/fabric/roughness.png";
        fabric.metallic = "D:/Library/assets/items/sofa/textures/maps/fabric/metallic.png";
        BuildAssetMaterial glass = fabric;
        glass.name = "Glass.001";
        glass.opacity = "D:/Library/assets/items/sofa/textures/maps/glass_001/opacity.png";
        state.materials = {fabric, glass};
        return state;
    };
    const BuildPlan plan = makeBuildPlan(project, source, sampleMaterialSource());
    REQUIRE(plan.assets.size() == 1);
    REQUIRE(plan.assets[0].materials.size() == 2);
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    const Json& materials = job["params"]["assets"][0]["materials"];
    REQUIRE(materials.size() == 2);
    REQUIRE(materials[0]["name"] == "Fabric");
    REQUIRE(materials[0]["maps"]["base_color"] == "D:/Library/assets/items/sofa/textures/maps/fabric/base_color.png");
    REQUIRE(materials[0]["maps"]["opacity"].is_null());
    REQUIRE(materials[0]["maps"]["occlusion"].is_null());
    REQUIRE(materials[1]["name"] == "Glass.001");
    REQUIRE(materials[1]["maps"]["opacity"] == "D:/Library/assets/items/sofa/textures/maps/glass_001/opacity.png");

    const BuildPlan noMaps = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    const Json bare = makeBuildJob(project, noMaps, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    REQUIRE(bare["params"]["assets"][0]["materials"].is_array());
    REQUIRE(bare["params"]["assets"][0]["materials"].empty());
    REQUIRE(jobProblem(bare).empty());
}

TEST_CASE("readBuildResult without counts.collision is BadFile", "[core][build][collision]") {
    Json result = readData("result-valid.json");
    REQUIRE(readBuildResult(result, "build-00000001").counts.collision == 31);
    result["output"]["counts"].erase("collision");
    const BuildWorkerOutput bad = readBuildResult(result, "build-00000001");
    REQUIRE(bad.header.status.code == ErrorCode::BadFile);
    REQUIRE(bad.header.status.where == "/output/counts/collision");
    REQUIRE(plynth::test::schemaProblem("blender-result.schema.json", nlohmann::json::parse(result.dump())).size() > 0);
}

namespace {

// The shape of a kind in a floor's collision, or null.
const CollisionShape* shapeOf(const std::vector<CollisionShape>& shapes, CollisionTargetKind kind, const Id& id = {})
{
    for (const CollisionShape& s : shapes) {
        if (s.kind == kind && (id.empty() || s.id == id)) return &s;
    }
    return nullptr;
}

// The area of the pieces of a shape.
double pieceArea(const CollisionShape& shape)
{
    double sum = 0.0;
    for (const CollisionPiece& p : shape.pieces) sum += signedArea(p.outline);
    return sum;
}

// A material source that has every id.
BuildMaterialSource anyMaterial()
{
    return [](std::string_view id) -> std::optional<BuildMaterialState> {
        BuildMaterialState state;
        state.material.id = std::string(id);
        state.material.baseColor = "D:/Library/" + std::string(id) + ".png";
        return state;
    };
}

}

TEST_CASE("the slab and stair collision of a two floor plan", "[core][build][collision][stairs]") {
    const Project project = stairProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    const FloorBuild& f1 = plan.floors[0];
    const std::vector<CollisionShape>& shapes = plan.collision[0];

    const CollisionShape* slab = shapeOf(shapes, CollisionTargetKind::Slab, "f1");
    REQUIRE(slab);
    REQUIRE(pieceArea(*slab) == Approx(worldArea(f1.slab.polygons)).margin(1e-6));
    for (const CollisionPiece& p : slab->pieces) {
        REQUIRE(isConvexRing(p.outline));
        REQUIRE(p.bottom == Approx(f1.slab.bottom));
        REQUIRE(p.top == Approx(f1.slab.top));
    }

    const CollisionShape* stair = shapeOf(shapes, CollisionTargetKind::Stair, "s1");
    REQUIRE(stair);
    REQUIRE(stair->pieces.size() == f1.stairs[0].steps.size());
    for (std::size_t i = 0; i < stair->pieces.size(); ++i) {
        REQUIRE(stair->pieces[i].bottom == Approx(f1.stairs[0].steps[i].bottom));
        REQUIRE(stair->pieces[i].top == Approx(f1.stairs[0].steps[i].top));
        REQUIRE(isConvexRing(stair->pieces[i].outline));
    }
    REQUIRE(stair->hulls.empty());
}

TEST_CASE("the ceiling collision leaves the stair hole open", "[core][build][collision][stairs]") {
    const Project project = stairProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    const FloorBuild& f1 = plan.floors[0];
    const CollisionShape* ceiling = shapeOf(plan.collision[0], CollisionTargetKind::Ceiling, f1.rooms[0].id);
    REQUIRE(ceiling);
    REQUIRE(pieceArea(*ceiling) == Approx(worldArea(f1.rooms[0].ceilingPolygons)).margin(1e-6));
    REQUIRE(pieceArea(*ceiling) < worldArea(f1.rooms[0].polygons) - 2.0);
    const CollisionShape* floorShape = shapeOf(plan.collision[0], CollisionTargetKind::Floor, f1.rooms[0].id);
    REQUIRE(floorShape);
    REQUIRE(pieceArea(*floorShape) == Approx(worldArea(f1.rooms[0].polygons)).margin(1e-6));
}

TEST_CASE("a flat roof has a hull per covering piece and prisms for its parapet", "[core][build][collision][roofs]") {
    const Project project = stairProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    const CollisionShape* roof = shapeOf(plan.collision[1], CollisionTargetKind::Roof, "f2");
    REQUIRE(roof);
    REQUIRE(roof->index == plan.floors[1].roofs[0].index);
    REQUIRE_FALSE(roof->hulls.empty());
    REQUIRE_FALSE(roof->pieces.empty());
    for (const std::vector<Vec3>& hull : roof->hulls) {
        REQUIRE(hull.size() >= 6);
        const std::size_t half = hull.size() / 2;
        for (std::size_t i = 0; i < half; ++i) {
            REQUIRE(hull[i].z - hull[i + half].z == Approx(kRoofThickness).margin(1e-9));
        }
    }
    for (const CollisionPiece& p : roof->pieces) {
        REQUIRE(isConvexRing(p.outline));
        REQUIRE(p.top > p.bottom);
    }
    REQUIRE(shapeOf(plan.collision[0], CollisionTargetKind::Roof) == nullptr);
}

TEST_CASE("the hulls of a gable roof lie on the covering planes", "[core][build][collision][roofs]") {
    Project project = stairProject();
    project.plan.roof.type = "gable";
    const BuildPlan plan = makeBuildPlan(project, {});
    const FloorBuild& f2 = plan.floors[1];
    REQUIRE(f2.roofs.size() == 1);
    const CollisionShape* roof = shapeOf(plan.collision[1], CollisionTargetKind::Roof, "f2");
    REQUIRE(roof);
    REQUIRE(roof->pieces.empty());
    REQUIRE(roof->hulls.size() >= 2);
    for (const std::vector<Vec3>& hull : roof->hulls) {
        const std::size_t half = hull.size() / 2;
        bool onSome = false;
        for (const RoofFace& face : f2.roofs[0].faces) {
            if (face.part != RoofPart::Covering || face.outline.size() < 3) continue;
            const Vec3& a = face.outline[0];
            const Vec3& b = face.outline[1];
            const Vec3& c = face.outline[2];
            const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
            const Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
            const Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
            const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (length < 1e-9) continue;
            bool all = true;
            for (std::size_t i = 0; i < half; ++i) {
                const double d = ((hull[i].x - a.x) * n.x + (hull[i].y - a.y) * n.y + (hull[i].z - a.z) * n.z) / length;
                if (std::fabs(d) > 1e-6) all = false;
            }
            if (all) onSome = true;
        }
        REQUIRE(onSome);
    }
}

TEST_CASE("the slots of stairs, slabs and roofs follow MaterialSlots.h", "[core][build][slots][stairs]") {
    Project project = stairProject();
    project.settings.materials = MaterialDefaults{};
    const BuildPlan flat = makeBuildPlan(project, {}, anyMaterial());
    REQUIRE(flat.floorMaterials[0].stairs[0].slot == "Stair");
    REQUIRE(flat.floorMaterials[0].slab.wellSlot == "Stair_Well");
    REQUIRE(flat.floorMaterials[1].roofs[0].coveringSlot == "Flat_Roof");
    REQUIRE(flat.floorMaterials[1].roofs[0].parapetSlot == "Exterior_Wall");
    REQUIRE_FALSE(flat.floorMaterials[1].roofs[0].parapet.empty());

    project.plan.roof.type = "gable";
    const BuildPlan gable = makeBuildPlan(project, {}, anyMaterial());
    const RoofMaterialIds& roof = gable.floorMaterials[1].roofs[0];
    REQUIRE(roof.coveringSlot == "Roof");
    REQUIRE_FALSE(roof.gable.empty());
    REQUIRE(roof.gableSlot == "Exterior_Wall");
    REQUIRE(roof.parapet.empty());
    REQUIRE(roof.glazing.empty());

    std::map<std::string, BuildSlot> table;
    for (const BuildSlot& slot : gable.materialSlots) table[slot.name] = slot;
    REQUIRE(table.count("Stair") == 1);
    REQUIRE(table.at("Stair").surface == "Stair");
    REQUIRE(table.at("Stair_Well").surface == "Wall_Inside");
    REQUIRE(table.at("Roof").surface == "Roof");
    for (const FloorMaterials& floor : gable.floorMaterials) {
        for (const StairMaterialId& stair : floor.stairs) REQUIRE(table.count(stair.slot) == 1);
        for (const RoofMaterialIds& r : floor.roofs) {
            for (const std::string* slot : {&r.coveringSlot, &r.undersideSlot, &r.edgeSlot, &r.gableSlot, &r.parapetSlot, &r.glazingSlot}) {
                REQUIRE(table.count(*slot) == 1);
            }
        }
        for (const std::string* slot : {&floor.slab.edgeSlot, &floor.slab.wellSlot, &floor.slab.undersideSlot}) {
            REQUIRE(table.count(*slot) == 1);
        }
    }
}

TEST_CASE("a part that is not built names no material", "[core][build][slots][stairs]") {
    Project project = stairProject();
    project.plan.floors[0].stairs.clear();
    const BuildPlan plan = makeBuildPlan(project, {}, anyMaterial());
    REQUIRE(plan.floorMaterials[0].slab.well.empty());
    REQUIRE(plan.floorMaterials[0].stairs.empty());
    for (const BuildSlot& slot : plan.materialSlots) REQUIRE(slot.name.rfind("Stair_Well_", 0) != 0);
}

TEST_CASE("two stairs with different materials get the material in the slot name", "[core][build][slots][stairs]") {
    Project project = stairProject();
    Stair second = project.plan.floors[0].stairs[0];
    second.id = "s2";
    second.type = "straight";
    second.x = 0.6;
    second.y = 0.4;
    second.material = "concrete";
    project.plan.floors[0].stairs[0].material = "oak_planks";
    project.plan.floors[0].stairs.push_back(second);
    const BuildPlan plan = makeBuildPlan(project, {}, anyMaterial());
    REQUIRE(plan.floors[0].stairs.size() == 2);
    REQUIRE(plan.floorMaterials[0].stairs[0].slot == "Stair_OakPlanks");
    REQUIRE(plan.floorMaterials[0].stairs[1].slot == "Stair_Concrete");
    std::set<std::string> names;
    for (const BuildSlot& slot : plan.materialSlots) names.insert(slot.name);
    REQUIRE(names.count("Stair") == 0);
    REQUIRE(names.count("Stair_OakPlanks") == 1);
}

TEST_CASE("the job writes the slots of slabs, stairs and roofs and validates", "[core][build][slots][stairs]") {
    plynth::test::TempDir tmp("job-slots-m12");
    Project project = stairProject();
    project.plan.roof.type = "gable";
    const BuildPlan plan = makeBuildPlan(project, {}, anyMaterial());
    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    const Json& f1 = job["params"]["floors"][0];
    REQUIRE(f1["stairs"][0]["slot"] == "Stair");
    REQUIRE(f1["slab"]["slots"]["well"] == "Stair_Well");
    REQUIRE(f1["slab"]["slots"]["edge"] == "Exterior_Wall");
    const Json& f2 = job["params"]["floors"][1];
    for (const Json& face : f2["roofs"][0]["faces"]) REQUIRE(face.contains("slot"));
    REQUIRE(f2["roofs"][0]["faces"][0]["slot"] == "Roof");

    std::set<std::string> table;
    for (const Json& slot : job["params"]["slots"]) table.insert(slot["name"].get<std::string>());
    REQUIRE(table.count("Stair") == 1);
    REQUIRE(table.count(f1["slab"]["slots"]["underside"].get<std::string>()) == 1);

    bool slabShape = false;
    bool stairShape = false;
    for (const Json& shape : f1["collision"]) {
        if (shape["target"]["kind"] == "slab") slabShape = true;
        if (shape["target"]["kind"] == "stair") stairShape = true;
    }
    REQUIRE(slabShape);
    REQUIRE(stairShape);
    bool roofShape = false;
    for (const Json& shape : f2["collision"]) {
        if (shape["target"]["kind"] != "roof") continue;
        roofShape = true;
        REQUIRE(shape["index"] == 0);
        REQUIRE_FALSE(shape["hulls"].empty());
    }
    REQUIRE(roofShape);
}

namespace {

// A floor's collision shapes of one kind.
std::vector<const CollisionShape*> shapesOfKind(const std::vector<CollisionShape>& shapes, CollisionTargetKind kind)
{
    std::vector<const CollisionShape*> out;
    for (const CollisionShape& s : shapes) {
        if (s.kind == kind) out.push_back(&s);
    }
    return out;
}

// The stair plan with a third floor above the second and the given slab thickness under each floor.
Project slabbedProject(double first, double second)
{
    Project project = stairProject();
    Floor third = project.plan.floors[1];
    third.id = "f3";
    third.name = "Floor 3";
    for (Point& p : third.points) p.id += "v";
    for (Wall& w : third.walls) {
        w.id += "v";
        w.from += "v";
        w.to += "v";
    }
    for (Room& r : third.rooms) {
        r.id += "v";
        for (Id& w : r.walls) w += "v";
    }
    project.plan.floors.push_back(third);
    project.plan.floors[0].slabThickness = first;
    project.plan.floors[1].slabThickness = second;
    return project;
}

}

TEST_CASE("no collision under a floor rises above the next level, whatever the slab", "[core][build][collision][stairs]") {
    for (const double slab : {0.05, 0.1, 0.2}) {
        DYNAMIC_SECTION("slab " << slab) {
            const Project project = slabbedProject(slab, slab);
            const BuildPlan plan = makeBuildPlan(project, {});
            REQUIRE(plan.floors.size() == 3);
            for (std::size_t i = 0; i + 1 < plan.floors.size(); ++i) {
                const double nextLevel = plan.floors[i + 1].elevation;
                const double ceilingZ = plan.floors[i].elevation + plan.floors[i].ceilingHeight;
                for (const CollisionShape& shape : plan.collision[i]) {
                    if (shape.kind == CollisionTargetKind::Walls || shape.kind == CollisionTargetKind::Stair ||
                        shape.kind == CollisionTargetKind::Roof) {
                        continue;
                    }
                    for (const CollisionPiece& piece : shape.pieces) REQUIRE(piece.top <= nextLevel + 1e-9);
                }
                for (const CollisionShape* ceiling : shapesOfKind(plan.collision[i], CollisionTargetKind::Ceiling)) {
                    for (const CollisionPiece& piece : ceiling->pieces) {
                        REQUIRE(piece.top == Approx(ceilingZ + std::min(kCollisionSlabThickness, slab)).margin(1e-9));
                    }
                }
                for (const CollisionShape* shape : shapesOfKind(plan.collision[i + 1], CollisionTargetKind::Floor)) {
                    for (const CollisionPiece& piece : shape->pieces) REQUIRE(piece.bottom >= ceilingZ - 1e-9);
                }
                for (const CollisionShape* shape : shapesOfKind(plan.collision[i + 1], CollisionTargetKind::Thresholds)) {
                    for (const CollisionPiece& piece : shape->pieces) REQUIRE(piece.bottom >= ceilingZ - 1e-9);
                }
            }
        }
    }
}

TEST_CASE("each floor's pieces go down by the slab of the floor below, the lowest by 0.1", "[core][build][collision][stairs]") {
    const Project project = slabbedProject(0.05, 0.2);
    const BuildPlan plan = makeBuildPlan(project, {});
    REQUIRE_FALSE(plan.floors[0].slabBelow.has_value());
    REQUIRE(plan.floors[1].slabBelow == 0.05);
    REQUIRE(plan.floors[2].slabBelow == 0.2);
    const std::vector<double> depth = {kCollisionSlabThickness, 0.05, kCollisionSlabThickness};
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        const auto floors = shapesOfKind(plan.collision[i], CollisionTargetKind::Floor);
        REQUIRE_FALSE(floors.empty());
        for (const CollisionShape* shape : floors) {
            for (const CollisionPiece& piece : shape->pieces) {
                REQUIRE(piece.top - piece.bottom == Approx(depth[i]).margin(1e-9));
            }
        }
        for (const CollisionShape* shape : shapesOfKind(plan.collision[i], CollisionTargetKind::Thresholds)) {
            for (const CollisionPiece& piece : shape->pieces) {
                REQUIRE(piece.top - piece.bottom == Approx(depth[i]).margin(1e-9));
            }
        }
        const auto ceilings = shapesOfKind(plan.collision[i], CollisionTargetKind::Ceiling);
        const double up = i == 0 ? 0.05 : (i == 1 ? kCollisionSlabThickness : kCollisionSlabThickness);
        for (const CollisionShape* shape : ceilings) {
            for (const CollisionPiece& piece : shape->pieces) {
                REQUIRE(piece.top - piece.bottom == Approx(up).margin(1e-9));
            }
        }
    }
}
