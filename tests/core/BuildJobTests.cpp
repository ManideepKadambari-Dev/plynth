#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
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
    REQUIRE(plan.floors[1].elevation == 3.0);
    REQUIRE(plan.floors[1].rooms[0].floorZ == 3.0);
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
    REQUIRE(out.counts == BuildCounts{1, 9, 7, 5, 4});
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
