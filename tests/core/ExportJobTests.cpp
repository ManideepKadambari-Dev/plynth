#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/ExportJob.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// The path of an export test data file.
fs::path dataFile(const std::string& name)
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / "export" / name;
}

// A data file as ordered JSON.
Json readData(const std::string& name)
{
    return Json::parse(plynth::test::readTextFile(dataFile(name)));
}

// Checks a job against the job schema.
std::string jobProblem(const Json& job)
{
    return plynth::test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump()));
}

// Checks a result against the result schema.
std::string resultProblem(const Json& result)
{
    return plynth::test::schemaProblem("blender-result.schema.json", nlohmann::json::parse(result.dump()));
}

// The built materials of the fixture, read the way the export reads them.
std::vector<BuiltMaterial> fixtureMaterials(const TempDir& tmp)
{
    fs::create_directories(tmp.path() / "build");
    fs::copy_file(dataFile("materials.json"), tmp.path() / "build" / "materials.json", fs::copy_options::overwrite_existing);
    const BuiltMaterialsRead read = readBuiltMaterials(tmp.path());
    requireOk(read.status);
    return read.materials;
}

}

TEST_CASE("quality status and phase names round trip", "[core][export]") {
    for (const QualityStatus s : {QualityStatus::Pass, QualityStatus::Warning, QualityStatus::Fail, QualityStatus::Skipped}) {
        REQUIRE(qualityStatusFromName(qualityStatusName(s)) == s);
    }
    REQUIRE(qualityStatusName(QualityStatus::Fail) == "fail");
    REQUIRE_FALSE(qualityStatusFromName("maybe"));
    REQUIRE(qualityPhaseFromName("scene") == QualityPhase::Scene);
    REQUIRE(qualityPhaseFromName("file") == QualityPhase::File);
    REQUIRE(qualityPhaseName(QualityPhase::File) == "file");
    REQUIRE_FALSE(qualityPhaseFromName("later"));
}

TEST_CASE("expectedWallBounds of the square room", "[core][export]") {
    const std::optional<ExportBounds> bounds = expectedWallBounds(squareProjectWithWalls());
    REQUIRE(bounds);
    REQUIRE(bounds->min.x == Approx(-0.1));
    REQUIRE(bounds->min.y == Approx(-4.1));
    REQUIRE(bounds->min.z == Approx(0.0));
    REQUIRE(bounds->max.x == Approx(4.1));
    REQUIRE(bounds->max.y == Approx(0.1));
    REQUIRE(bounds->max.z == Approx(3.0));
    REQUIRE_FALSE(expectedWallBounds(makeNewProject()));
}

TEST_CASE("expectedWallBounds follows the floor elevation", "[core][export]") {
    Project project = squareProjectWithWalls();
    Floor upper = squareFloorWithWalls();
    upper.id = "f2";
    upper.name = "Floor 2";
    project.plan.floors.push_back(upper);
    const std::optional<ExportBounds> bounds = expectedWallBounds(project);
    REQUIRE(bounds);
    REQUIRE(bounds->min.z == Approx(0.0));
    REQUIRE(bounds->max.z > 3.0);
}

TEST_CASE("makeExportJob validates against the job schema", "[core][export][schema]") {
    TempDir tmp("exportjob-valid");
    Project project = squareProjectWithWalls();
    project.settings.name = "Flat";
    std::vector<BuiltMaterial> materials = fixtureMaterials(tmp);
    REQUIRE(materials.size() == 3);

    ExportBuildState build;
    build.reportFound = true;
    build.planChanged = false;
    BuildProblem problem;
    problem.code = "cut_failed";
    problem.severity = BuildSeverity::Error;
    problem.message = "The cut for opening o3 failed.";
    problem.floor = "f1";
    problem.object = "o3";
    problem.step = "openings";
    build.problems.push_back(problem);

    ExportGroup group;
    group.id = "building";
    group.file = "Flat.fbx";
    ExportGroup second;
    second.id = "walls";
    second.file = "Walls.fbx";
    second.kinds = {"wall"};
    second.ids = {"w1", "w2"};
    second.name = "Walls";
    second.members = {{"walls", "f1"}, {"slab", ""}};
    second.rest = true;

    const fs::path staging = tmp.path() / "export" / ".staging";
    const Json job = makeExportJob(project, {group, second}, materials, build, ExportSettings{true, false}, tmp.path(), staging,
                                   "export-1a2b3c4d", tmp.path() / "build" / "export_work" / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());

    REQUIRE(job["task"] == "export");
    REQUIRE(job["job_id"] == "export-1a2b3c4d");
    const Json& params = job["params"];
    REQUIRE(params["project"]["name"] == "Flat");
    REQUIRE(params["project"]["folder"] == pathToUtf8(tmp.path()));
    REQUIRE(params["blend_file"] == pathToUtf8(tmp.path() / "build" / "building.blend"));
    REQUIRE(params["output_folder"] == pathToUtf8(staging));
    REQUIRE(params["textures_folder"] == "Textures");
    REQUIRE(params["groups"].size() == 2);
    REQUIRE(params["groups"][1]["kinds"] == Json::array({"wall"}));
    REQUIRE(params["groups"][1]["ids"] == Json::array({"w1", "w2"}));
    REQUIRE(params["groups"][0].contains("name") == false);
    REQUIRE(params["groups"][0]["members"].empty());
    REQUIRE(params["groups"][0]["rest"] == false);
    REQUIRE(params["groups"][1]["name"] == "Walls");
    REQUIRE(params["groups"][1]["members"][0] == Json{{"kind", "walls"}, {"id", "f1"}});
    REQUIRE(params["groups"][1]["members"][1] == Json{{"kind", "slab"}, {"id", nullptr}});
    REQUIRE(params["groups"][1]["rest"] == true);
    REQUIRE(params["kit"].is_null());
    REQUIRE(params["materials"].size() == 3);
    REQUIRE(params["materials"][0]["name"] == "Living_Floor");
    REQUIRE(params["materials"][0]["maps"]["opacity"].is_null());
    REQUIRE(params["materials"][1]["alpha_mode"] == "blend");
    REQUIRE_FALSE(params["materials"][0].contains("asset"));
    REQUIRE(params["expected_bounds"]["min"][1].get<double>() == Approx(-4.1));
    REQUIRE(params["expected_bounds"]["max"][2].get<double>() == Approx(3.0));
    REQUIRE(params["build"]["report_found"] == true);
    REQUIRE(params["build"]["plan_changed"] == false);
    REQUIRE(params["build"]["problems"].size() == 1);
    REQUIRE(params["build"]["problems"][0]["object"] == "o3");
    REQUIRE(params["limits"]["object_triangles"] == kObjectTriangleWarning);
    REQUIRE(params["limits"]["total_triangles"] == kTotalTriangleWarning);
    REQUIRE(params["limits"]["texel_warning_ratio"].get<double>() == Approx(kTexelWarningRatio));
    REQUIRE(params["limits"]["texel_fail_ratio"].get<double>() == Approx(kTexelFailRatio));
    REQUIRE(params["limits"]["bounds_tolerance"].get<double>() == Approx(kBoundsTolerance));
    REQUIRE(params["force"] == true);
    REQUIRE(params["write"] == false);
}

TEST_CASE("makeExportJob keeps the asset of a furniture material", "[core][export][schema]") {
    TempDir tmp("exportjob-asset");
    BuiltMaterial furniture;
    furniture.name = "Fabric";
    furniture.asset = "sofa";
    furniture.baseColor = tmp.path() / "base.png";
    furniture.normal = tmp.path() / "normal.png";
    furniture.roughness = tmp.path() / "roughness.png";
    furniture.metallic = tmp.path() / "metallic.png";
    ExportGroup group;
    group.id = "building";
    group.file = "Untitled.fbx";
    const Json job = makeExportJob(squareProjectWithWalls(), {group}, {furniture}, ExportBuildState{}, ExportSettings{}, tmp.path(),
                                   tmp.path() / "export" / ".staging", "export-00000003", tmp.path() / "result.json");
    REQUIRE(jobProblem(job).empty());
    REQUIRE(job["params"]["materials"][0]["asset"] == "sofa");
    REQUIRE(job["params"]["materials"][0]["material"].is_null());
}

TEST_CASE("makeExportJob with no walls and no materials validates", "[core][export][schema]") {
    TempDir tmp("exportjob-empty");
    ExportGroup group;
    group.id = "building";
    group.file = "Untitled.fbx";
    const Json job = makeExportJob(makeNewProject(), {group}, {}, ExportBuildState{}, ExportSettings{}, tmp.path(),
                                   tmp.path() / "export" / ".staging", "export-00000002", tmp.path() / "result.json");
    INFO(jobProblem(job));
    REQUIRE(jobProblem(job).empty());
    REQUIRE(job["params"]["expected_bounds"].is_null());
    REQUIRE(job["params"]["materials"].empty());
    REQUIRE(job["params"]["build"]["report_found"] == false);
    REQUIRE(job["params"]["build"]["plan_changed"] == true);
    REQUIRE(job["params"]["force"] == false);
    REQUIRE(job["params"]["write"] == true);
}

TEST_CASE("the materials fixture validates against the built materials schema", "[core][export][schema]") {
    REQUIRE(plynth::test::schemaProblem("build-materials.schema.json", nlohmann::json::parse(readData("materials.json").dump())).empty());
}

TEST_CASE("readExportResult reads a valid result", "[core][export]") {
    const Json result = readData("result-valid.json");
    INFO(resultProblem(result));
    REQUIRE(resultProblem(result).empty());
    const ExportWorkerOutput out = readExportResult(result, "export-00000001");
    requireOk(out.header.status);
    REQUIRE(out.header.blenderVersion == "5.2.0");
    REQUIRE(out.written);
    REQUIRE(out.files.size() == 1);
    REQUIRE(out.files[0] == ExportFileOutput{"building", "Flat.fbx", 40, 18200});
    REQUIRE(out.textures == std::vector<std::string>{"T_OakPlanks_BaseColor.jpg", "T_PaintA_Normal.png"});
    REQUIRE(out.objects == 40);
    REQUIRE(out.triangles == 18200);
    REQUIRE(out.collisionObjects == 31);
    REQUIRE(out.checks.size() == 4);
    REQUIRE(out.checks[0].id == "build_current");
    REQUIRE(out.checks[0].status == QualityStatus::Pass);
    REQUIRE(out.checks[1].status == QualityStatus::Fail);
    REQUIRE(out.checks[1].findings.size() == 2);
    REQUIRE(out.checks[1].findings[0] == QualityFinding{"SM_f1_Walls", "w3", "The name has a space."});
    REQUIRE(out.checks[1].findings[1].object.empty());
    REQUIRE(out.checks[1].findings[1].planId.empty());
    REQUIRE(out.checks[2].status == QualityStatus::Warning);
    REQUIRE(out.checks[3].phase == QualityPhase::File);
    REQUIRE(out.checks[3].status == QualityStatus::Skipped);
    REQUIRE(out.steps.size() == 2);
    REQUIRE(out.steps[1].step == "write_fbx");
    REQUIRE(out.steps[1].ms == 2900);
}

TEST_CASE("readExportResult refuses an unknown status", "[core][export]") {
    const Json result = readData("result-bad-status.json");
    REQUIRE_FALSE(resultProblem(result).empty());
    const ExportWorkerOutput out = readExportResult(result, "export-00000001");
    REQUIRE(out.header.status.code == ErrorCode::BadFile);
    REQUIRE(out.header.status.where == "/output/checks/1/status");
}

TEST_CASE("readExportResult refuses a file name with a folder", "[core][export]") {
    const Json result = readData("result-file-folder.json");
    REQUIRE_FALSE(resultProblem(result).empty());
    const ExportWorkerOutput out = readExportResult(result, "export-00000001");
    REQUIRE(out.header.status.code == ErrorCode::BadFile);
    REQUIRE(out.header.status.where == "/output/files/0/file");
    REQUIRE(out.files.empty());
}

TEST_CASE("readExportResult refuses a texture with a folder, a missing field and another job id", "[core][export]") {
    Json textures = readData("result-valid.json");
    textures["output"]["textures"][0] = "Textures/T_A.png";
    REQUIRE(readExportResult(textures, "export-00000001").header.status.where == "/output/textures/0");

    Json missing = readData("result-valid.json");
    missing["output"].erase("collision_objects");
    const ExportWorkerOutput out = readExportResult(missing, "export-00000001");
    REQUIRE(out.header.status.code == ErrorCode::BadFile);
    REQUIRE(out.header.status.where == "/output/collision_objects");

    REQUIRE(readExportResult(readData("result-valid.json"), "export-other").header.status.code == ErrorCode::BadFile);
}

TEST_CASE("readExportResult of a failed job is blender_failed with the code", "[core][export]") {
    Json result = readData("result-valid.json");
    result["ok"] = false;
    result["error"] = Json{{"code", "no_geometry"}, {"message", "The scene has nothing to export."}};
    result["output"] = nullptr;
    REQUIRE(resultProblem(result).empty());
    const ExportWorkerOutput out = readExportResult(result, "export-00000001");
    REQUIRE(out.header.status.code == ErrorCode::BlenderFailed);
    REQUIRE(out.header.errorCode == "no_geometry");
}

TEST_CASE("readExportResult reads a result with empty groups and a kit", "[core][export]") {
    Json result = readData("result-valid.json");
    REQUIRE(readExportResult(result, "export-00000001").emptyGroups.empty());
    REQUIRE_FALSE(readExportResult(result, "export-00000001").kit.has_value());

    result["output"]["empty_groups"] = Json::array({"furniture_r2"});
    result["output"]["kit"] = Json{{"file", "Flat_Kit.fbx"}, {"pieces", 3}, {"objects", 9}, {"triangles", 1200}};
    INFO(resultProblem(result));
    REQUIRE(resultProblem(result).empty());
    const ExportWorkerOutput out = readExportResult(result, "export-00000001");
    requireOk(out.header.status);
    REQUIRE(out.emptyGroups == std::vector<std::string>{"furniture_r2"});
    REQUIRE(out.kit.has_value());
    REQUIRE(*out.kit == ExportKitOutput{"Flat_Kit.fbx", 3, 9, 1200});

    result["output"]["kit"] = nullptr;
    REQUIRE_FALSE(readExportResult(result, "export-00000001").kit.has_value());
    result["output"]["kit"] = Json{{"file", "sub/Flat_Kit.fbx"}, {"pieces", 3}, {"objects", 9}, {"triangles", 1200}};
    REQUIRE(readExportResult(result, "export-00000001").header.status.code == ErrorCode::BadFile);
    result["output"]["kit"] = nullptr;
    result["output"]["empty_groups"] = Json::array({7});
    REQUIRE(readExportResult(result, "export-00000001").header.status.code == ErrorCode::BadFile);
}
