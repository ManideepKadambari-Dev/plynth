#include "BuildFixtures.h"
#include "MaterialFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Materials.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
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
    REQUIRE(out.counts == BuildCounts{1, 9, 7, 5, 4, 6, 2, 1, 3});
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
    REQUIRE(out.counts == BuildCounts{1, 9, 7, 5, 4, 6, 2, 1, 3});

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
    REQUIRE(plan.floorMaterials[0].slab.underside == "white_paint");
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
