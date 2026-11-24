#include "BuildFixtures.h"
#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Blender.h>
#include <plynth/core/Build.h>
#include <plynth/core/BuildPreview.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Generators.h>
#include <plynth/core/LicenseReport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// A library with a ready sofa, a project folder and a fake Blender, ready to build the build fixtures.
struct BuildFixture {
    explicit BuildFixture(const std::string& tag, const std::string& mode = "") : lib(tag)
    {
        projectFolder = lib.tmp.path() / "Project";
        fs::create_directories(projectFolder);
        const fs::path exe = makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", mode);
        request.projectFolder = projectFolder;
        request.project = buildProject();
        clearMaterialDefaults(request.project);
        request.library = lib.paths;
        request.blender = inspectBlender(exe);
        request.run.scriptsFolder = makeFakeScripts(lib.tmp.path() / "scripts");
        lib.add("sofa", "Sofa", {"sofa"});
    }

    fs::path lockFile() const { return projectFolder / "build" / "build.lock"; }
    fs::path reportFile() const { return projectFolder / "build" / "build.json"; }

    TestLibrary lib;
    fs::path projectFolder;
    BuildRequest request;
};

// Writes a lock file as a build would, for a process and a time.
void writeLock(const fs::path& file, const ProcessIdentity& who, long long epoch)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << who.pid << " " << who.startKey << " " << epoch << "\n";
}

// Seconds since 1970 now.
long long epochNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Checks a report file against the report schema.
std::string reportProblem(const fs::path& file)
{
    return plynth::test::schemaProblem("build-report.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file)));
}

}

TEST_CASE("runBuild through the fake Blender writes the files and the report", "[core][build][run]") {
    BuildFixture f("run-ok", "build_problem");
    std::vector<BuildStage> stages;
    int workerSteps = 0;
    int lastCount = 0;
    const BuildReport report = runBuild(f.request, [&](const BuildProgress& p) {
        if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
        if (p.stage == BuildStage::Blender && !p.blender.step.empty()) {
            ++workerSteps;
            lastCount = p.blender.count;
            REQUIRE(p.blender.index == workerSteps);
        }
    });
    plynth::test::requireOk(report.status);
    REQUIRE(stages == std::vector<BuildStage>{BuildStage::Preparing, BuildStage::Blender, BuildStage::Finishing, BuildStage::Done});
    REQUIRE(workerSteps == 8);
    REQUIRE(lastCount == 8);

    REQUIRE(report.jobId.rfind("build-", 0) == 0);
    REQUIRE(report.blenderVersion == "5.2.0");
    REQUIRE(report.planFingerprint == planFingerprint(f.request.project));
    REQUIRE(report.blendFile == f.projectFolder / "build" / "building.blend");
    REQUIRE(report.fbxFile == f.projectFolder / "build" / "building.fbx");
    REQUIRE(fs::exists(report.blendFile));
    REQUIRE(fs::exists(report.fbxFile));
    REQUIRE(report.materialsFile == f.projectFolder / "build" / "materials.json");
    REQUIRE(fs::exists(report.materialsFile));
    REQUIRE(fs::exists(f.projectFolder / "build" / "job.json"));
    REQUIRE(fs::exists(f.projectFolder / "build" / "result.json"));
    REQUIRE(fs::exists(f.projectFolder / "build" / "blender.log"));
    REQUIRE(report.logFile == f.projectFolder / "build" / "blender.log");
    REQUIRE_FALSE(fs::exists(f.lockFile()));

    // The fake counts every collision piece and hull of the job and one hull per item.
    const nlohmann::json sentJob = nlohmann::json::parse(plynth::test::readTextFile(f.projectFolder / "build" / "job.json"));
    int pieces = 0;
    for (const auto& floor : sentJob["params"]["floors"]) {
        for (const auto& shape : floor["collision"]) pieces += static_cast<int>(shape["pieces"].size() + shape["hulls"].size());
    }
    REQUIRE(pieces > 4);
    REQUIRE(report.counts == BuildCounts{1, 1, 1, 1, 2, 1, 1, 0, 1, pieces + 2});
    REQUIRE(report.objects == 1 + 1 + 2 + 2);
    REQUIRE(report.steps.size() == 9);
    REQUIRE(report.steps[0].step == "check");
    REQUIRE(report.steps[1].step == "prepare_scene");
    REQUIRE(report.steps[8].step == "export_fbx");

    // Core's problems first, then the worker's.
    REQUIRE(report.problems.size() == 7);
    REQUIRE(report.problems[0].step == "check");
    REQUIRE(report.problems[5].step == "check");
    REQUIRE(report.problems[6].code == "cut_failed");
    REQUIRE(report.problems[6].object == "o1");
    REQUIRE(report.problems[6].step == "openings");
    REQUIRE(report.errorCount() == 5);
    REQUIRE(report.warningCount() == 2);

    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["ok"] == true);
    REQUIRE(written["outputs"]["materials_file"] == pathToUtf8(report.materialsFile));
    REQUIRE(written["counts"]["materials"] == 1);
    REQUIRE(written["error_count"] == 5);
    REQUIRE(written["warning_count"] == 2);

    // The project folder's plan is never written.
    REQUIRE_FALSE(fs::exists(f.projectFolder / "plan.json"));
}

TEST_CASE("runBuild without an fbx or a library", "[core][build][run]") {
    BuildFixture f("run-nofbx");
    f.request.settings.writeFbx = false;
    f.request.library.reset();
    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(fs::exists(report.blendFile));
    REQUIRE(report.fbxFile.empty());
    REQUIRE(report.materialsFile.empty());
    REQUIRE_FALSE(fs::exists(f.projectFolder / "build" / "building.fbx"));
    REQUIRE_FALSE(fs::exists(f.projectFolder / "build" / "materials.json"));
    REQUIRE(nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()))["outputs"]["materials_file"].is_null());
    REQUIRE(report.problems[0].code == "no_library");
    REQUIRE(report.counts.items == 0);
    REQUIRE(reportProblem(f.reportFile()).empty());
}

TEST_CASE("a fresh lock held by a live process is in_use", "[core][build][run]") {
    BuildFixture f("run-locked");
    writeLock(f.lockFile(), currentProcess(), epochNow());
    const BuildReport report = runBuild(f.request);
    REQUIRE(report.status.code == ErrorCode::InUse);
    REQUIRE(fs::exists(f.lockFile()));
    REQUIRE_FALSE(fs::exists(f.reportFile()));
}

TEST_CASE("a lock with an old time or a dead process is taken over", "[core][build][run]") {
    BuildFixture f("run-stale");
    writeLock(f.lockFile(), currentProcess(), epochNow() - 7200);
    fs::last_write_time(f.lockFile(), fs::file_time_type::clock::now() - std::chrono::hours(2));
    plynth::test::requireOk(runBuild(f.request).status);
    REQUIRE_FALSE(fs::exists(f.lockFile()));

    writeLock(f.lockFile(), ProcessIdentity{999999, 1}, epochNow());
    plynth::test::requireOk(runBuild(f.request).status);
    REQUIRE_FALSE(fs::exists(f.lockFile()));
}

TEST_CASE("a failing worker gives blender_failed and a report with ok false", "[core][build][run]") {
    BuildFixture f("run-fail", "fail");
    const BuildReport report = runBuild(f.request);
    REQUIRE(report.status.code == ErrorCode::BlenderFailed);
    REQUIRE(report.errorCode == "save_failed");
    REQUIRE(report.status.message.find("Fake save failure.") != std::string::npos);
    REQUIRE(report.status.message.find("blender.log") != std::string::npos);
    REQUIRE(report.blendFile.empty());
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["ok"] == false);
    REQUIRE(written["error"]["code"] == "blender_failed");
    REQUIRE(written["error"]["worker_code"] == "save_failed");
    REQUIRE(written["outputs"]["blend_file"].is_null());
}

TEST_CASE("built-plan.json keeps the plan of the last successful build", "[core][build][run]") {
    BuildFixture f("run-built-plan");
    plynth::test::requireOk(runBuild(f.request).status);
    const fs::path file = f.projectFolder / "build" / "built-plan.json";
    const std::string first = plynth::test::readTextFile(file);
    const PlanRead read = planFromJson(Json::parse(first)["plan"]);
    INFO(read.status.message);
    REQUIRE(read.status.ok());
    REQUIRE(read.plan == f.request.project.plan);

    f.request.project.plan.floors[0].points[0].x -= 0.1;
    std::ofstream(f.lib.tmp.path() / "blender" / "fake_mode.txt") << "fail";
    REQUIRE(runBuild(f.request).status.code == ErrorCode::BlenderFailed);
    REQUIRE(plynth::test::readTextFile(file) == first);
}

TEST_CASE("used-items.json lists what the last successful build used", "[core][build][run][licenses]") {
    BuildFixture f("run-used-items");
    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    const fs::path file = f.projectFolder / "build" / "used-items.json";
    const UsedItemsRead read = readUsedItems(file);
    plynth::test::requireOk(read.status);
    CHECK(read.items.planFingerprint == report.planFingerprint);
    CHECK(std::is_sorted(read.items.assets.begin(), read.items.assets.end()));
    CHECK(std::is_sorted(read.items.materials.begin(), read.items.materials.end()));
    REQUIRE(plynth::test::schemaProblem("used-items.schema.json",
                                        nlohmann::json::parse(plynth::test::readTextFile(file))).empty());

    const std::string first = plynth::test::readTextFile(file);
    f.request.project.plan.floors[0].points[0].x -= 0.1;
    std::ofstream(f.lib.tmp.path() / "blender" / "fake_mode.txt") << "fail";
    REQUIRE(runBuild(f.request).status.code == ErrorCode::BlenderFailed);
    CHECK(plynth::test::readTextFile(file) == first);
}

TEST_CASE("a crashing Blender is blender_failed", "[core][build][run]") {
    BuildFixture f("run-crash", "crash");
    const BuildReport report = runBuild(f.request);
    REQUIRE(report.status.code == ErrorCode::BlenderFailed);
    REQUIRE(report.errorCode.empty());
    REQUIRE_FALSE(fs::exists(f.lockFile()));
}

TEST_CASE("stopping a build gives cancelled and removes the lock", "[core][build][run]") {
    BuildFixture f("run-stop", "hang");
    std::stop_source source;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        source.request_stop();
    });
    const BuildReport report = runBuild(f.request, {}, source.get_token());
    stopper.join();
    REQUIRE(report.status.code == ErrorCode::Cancelled);
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE_FALSE(fs::exists(f.reportFile()));
}

TEST_CASE("runBuild refuses before doing anything", "[core][build][run]") {
    BuildFixture f("run-refuse");
    BuildRequest missing = f.request;
    missing.projectFolder = f.lib.tmp.path() / "nowhere";
    REQUIRE(runBuild(missing).status.code == ErrorCode::NotFound);

    BuildRequest empty = f.request;
    empty.project = makeNewProject();
    const BuildReport noWalls = runBuild(empty);
    REQUIRE(noWalls.status.code == ErrorCode::InvalidValue);
    REQUIRE(noWalls.status.message == "The plan has no walls to build.");
    REQUIRE_FALSE(fs::exists(f.projectFolder / "build"));

    BuildRequest noBlender = f.request;
    noBlender.blender = BlenderInstall{};
    REQUIRE(runBuild(noBlender).status.code == ErrorCode::BlenderNotFound);
    REQUIRE_FALSE(fs::exists(f.projectFolder / "build" / "build.json"));
}

TEST_CASE("readBuildReport gives back an equal report", "[core][build][run]") {
    BuildFixture f("run-read", "build_problem");
    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    const BuildReportRead read = readBuildReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(buildReportToJson(read.report) == buildReportToJson(report));
    REQUIRE(read.report.blendFile == report.blendFile);
    REQUIRE(read.report.startedAt == report.startedAt);
    REQUIRE(read.report.problems == report.problems);
    REQUIRE(read.report.counts == report.counts);
    REQUIRE(read.report.steps == report.steps);

    BuildFixture failed("run-read-failed", "fail");
    const BuildReport bad = runBuild(failed.request);
    const BuildReportRead badRead = readBuildReport(failed.projectFolder);
    plynth::test::requireOk(badRead.status);
    REQUIRE(badRead.report.status.code == ErrorCode::BlenderFailed);
    REQUIRE(badRead.report.errorCode == "save_failed");
    REQUIRE(buildReportToJson(badRead.report) == buildReportToJson(bad));
}

TEST_CASE("readBuildReport says not_found and bad_file", "[core][build][run]") {
    plynth::test::TempDir tmp("build-read-bad");
    REQUIRE(readBuildReport(tmp.path()).status.code == ErrorCode::NotFound);
    fs::create_directories(tmp.path() / "build");
    std::ofstream(tmp.path() / "build" / "build.json") << "{not json";
    REQUIRE(readBuildReport(tmp.path()).status.code == ErrorCode::BadFile);
    std::ofstream(tmp.path() / "build" / "build.json", std::ios::trunc) << R"({"format":"plynth-build-report","version":1})";
    const BuildReportRead missing = readBuildReport(tmp.path());
    REQUIRE(missing.status.code == ErrorCode::BadFile);
}

TEST_CASE("readBuildReport rejects a failed report with no error", "[core][build][run]") {
    BuildFixture failed("run-read-noerror", "fail");
    runBuild(failed.request);
    const fs::path file = failed.projectFolder / "build" / "build.json";
    nlohmann::json json = nlohmann::json::parse(plynth::test::readTextFile(file));
    json.erase("error");
    std::ofstream(file, std::ios::trunc) << json.dump();
    const BuildReportRead read = readBuildReport(failed.projectFolder);
    REQUIRE(read.status.code == ErrorCode::BadFile);
    REQUIRE(read.status.message.find("/error") != std::string::npos);
}

TEST_CASE("planFingerprint changes when the plan or settings change", "[core][build]") {
    const Project a = buildProject();
    Project b = buildProject();
    const std::string print = planFingerprint(a);
    REQUIRE(print.size() == 16);
    REQUIRE(print == planFingerprint(b));
    b.plan.floors[0].points[0].x += 0.01;
    REQUIRE(planFingerprint(b) != print);
    Project c = buildProject();
    c.settings.gridSize = 0.25;
    REQUIRE(planFingerprint(c) != print);
}

namespace {

// The hash of the plan and the settings exactly as they are written, export mode and all.
std::string writtenHash(const Project& project)
{
    const std::string text = planToJson(project.plan).dump() + "\n" + settingsToJson(project.settings).dump();
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(hash));
    return out;
}

}

TEST_CASE("planFingerprint ignores the export mode and hashes a single project as it is written", "[core][build][settings]") {
    Project single = buildProject();
    Project grouped = buildProject();
    grouped.settings.exportMode = ExportMode::Grouped;
    REQUIRE(planFingerprint(grouped) == planFingerprint(single));
    REQUIRE(planFingerprint(single) == writtenHash(single));
}

TEST_CASE("problemPosition finds the object on the plan", "[core][build]") {
    const Project project = buildProject();
    const auto at = [&](const std::string& object) {
        BuildProblem p;
        p.object = object;
        p.floor = "f1";
        return problemPosition(project.plan, p);
    };
    const auto point = at("p2");
    REQUIRE(point);
    REQUIRE(point->x == Approx(4.0));
    REQUIRE(point->y == Approx(0.0));
    const auto wall = at("w1");
    REQUIRE(wall);
    REQUIRE(wall->x == Approx(2.0));
    REQUIRE(wall->y == Approx(0.0));
    const auto opening = at("o1");
    REQUIRE(opening);
    REQUIRE(opening->x == Approx(2.0));
    REQUIRE(opening->y == Approx(0.0));
    const auto room = at("r1");
    REQUIRE(room);
    REQUIRE(room->x == Approx(2.0).margin(0.6));
    REQUIRE(room->y == Approx(2.0).margin(0.6));
    const auto item = at("i1");
    REQUIRE(item);
    REQUIRE(item->x == Approx(2.0));
    REQUIRE(item->y == Approx(2.5));
    const auto open = at("r2");
    REQUIRE(open);

    REQUIRE_FALSE(at(""));
    REQUIRE_FALSE(at("w99"));
}

TEST_CASE("buildAssetSourceFor reports ready, missing and broken assets", "[core][build]") {
    TestLibrary lib("build-source");
    lib.add("sofa", "Sofa");
    lib.add("lamp", "Lamp");
    fs::remove(lib.paths.assetItems / "lamp" / "asset.blend");
    lib.catalog->rescan();
    const BuildAssetSource source = buildAssetSourceFor(*lib.catalog);

    const auto ready = source("sofa");
    REQUIRE(ready);
    REQUIRE_FALSE(ready->broken);
    REQUIRE(ready->blendFile == lib.paths.assetItems / "sofa" / "asset.blend");

    REQUIRE_FALSE(source("chair"));

    const auto broken = source("lamp");
    REQUIRE(broken);
    REQUIRE(broken->broken);
    REQUIRE_FALSE(broken->problem.empty());
}

namespace {

// A project for the Blender build tests: two rooms side by side with a divider wall, one curved wall, one
// rounded corner, one opening of each built in shape and a door on the curved wall. No items.
Project blenderFixtureProject()
{
    Project project = makeNewProject();
    project.settings.name = "Fixture";
    clearMaterialDefaults(project);
    Floor f;
    f.id = "f1";
    f.name = "Floor 1";
    f.points = {{"p1", 0.0, 0.0, 0.6}, {"p2", 4.0, 0.0, 0.0}, {"p3", 8.0, 0.0, 0.0},
                {"p4", 8.0, 4.0, 0.0}, {"p5", 4.0, 4.0, 0.0}, {"p6", 0.0, 4.0, 0.0}};
    const char* ends[7][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p5"}, {"p5", "p6"}, {"p6", "p1"}, {"p2", "p5"}};
    for (int i = 0; i < 7; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.2;
        if (w.id == "w3") w.bulge = -0.5;
        f.walls.push_back(w);
    }
    Room left;
    left.id = "r1";
    left.type = "living";
    left.walls = {"w1", "w7", "w5", "w6"};
    Room right;
    right.id = "r2";
    right.type = "bedroom";
    right.walls = {"w2", "w3", "w4", "w7"};
    f.rooms = {left, right};

    const auto opening = [&](const std::string& id, const std::string& wall, double offset, OpeningType type,
                             const std::string& shape, double width, double height, double sill) {
        Opening o;
        o.id = id;
        o.wall = wall;
        o.offset = offset;
        o.type = type;
        o.shape = shape;
        o.width = width;
        o.height = height;
        o.sill = sill;
        f.openings.push_back(o);
    };
    opening("o1", "w1", 2.0, OpeningType::Door, "rectangle", 0.9, 2.1, 0.0);
    opening("o2", "w5", 2.0, OpeningType::Window, "round_arch", 1.0, 1.4, 0.9);
    opening("o3", "w6", 2.0, OpeningType::Window, "flat_arch", 1.2, 1.2, 0.9);
    opening("o4", "w4", 2.0, OpeningType::Plain, "pointed_arch", 1.0, 2.0, 0.0);
    opening("o5", "w2", 2.0, OpeningType::Window, "circle", 1.0, 1.0, 1.0);
    opening("o6", "w3", 2.1, OpeningType::Door, "rectangle", 0.9, 2.1, 0.0);
    opening("o7", "w7", 2.0, OpeningType::Door, "rectangle", 0.9, 2.1, 0.0);
    project.plan.floors[0] = f;
    return project;
}

// The job of the Blender fixture, with the project folder left as a placeholder the Blender tests fill in.
Json blenderFixtureJob()
{
    const Project project = blenderFixtureProject();
    const BuildPlan plan = makeBuildPlan(project, {});
    return makeBuildJob(project, plan, BuildSettings{}, "@PROJECT@", "build-fixture", "@PROJECT@/build/result.json");
}

}

TEST_CASE("the job made for the Blender tests builds with no problems and matches the saved copy", "[core][build][fixture]") {
    const BuildPlan plan = makeBuildPlan(blenderFixtureProject(), {});
    REQUIRE(plan.problems.empty());
    REQUIRE(plan.floors[0].openings.size() == 7);
    REQUIRE(plan.floors[0].rooms.size() == 2);
    REQUIRE(plan.floors[0].wallSolids.size() == 1);

    const Json job = blenderFixtureJob();
    REQUIRE(plynth::test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump())).empty());
    const fs::path saved = fs::path(PLYNTH_TEST_DATA_DIR) / "buildjob" / "from_core.json";
    INFO("Regenerate with: plynth_core_tests \"[.write-fixture]\"");
    REQUIRE(plynth::test::readTextFile(saved) == job.dump(2) + "\n");
}

TEST_CASE("write the job for the Blender tests", "[.write-fixture]") {
    const fs::path saved = fs::path(PLYNTH_TEST_DATA_DIR) / "buildjob" / "from_core.json";
    std::ofstream(saved, std::ios::binary | std::ios::trunc) << blenderFixtureJob().dump(2) << "\n";
    REQUIRE(fs::exists(saved));
}

TEST_CASE("the build report keeps materials_file and the materials count", "[core][build][run][materials]") {
    BuildFixture f("run-materials");
    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(reportProblem(f.reportFile()).empty());
    REQUIRE(report.counts.materials == 1);

    const BuildReportRead read = readBuildReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.materialsFile == report.materialsFile);
    REQUIRE(read.report.counts.materials == 1);
    REQUIRE(buildReportToJson(read.report) == buildReportToJson(report));
}

TEST_CASE("a report written by milestone 8 reads with no materials", "[core][build][materials]") {
    plynth::test::TempDir tmp("build-report-m8");
    fs::create_directories(tmp.path() / "build");
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "build" / "build-m8.json", tmp.path() / "build" / "build.json");
    REQUIRE(plynth::test::schemaProblem("build-report.schema.json",
                                        nlohmann::json::parse(plynth::test::readTextFile(tmp.path() / "build" / "build.json")))
                .empty());
    const BuildReportRead read = readBuildReport(tmp.path());
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.counts.materials == 0);
    REQUIRE(read.report.materialsFile.empty());
    REQUIRE(read.report.counts.items == 1);
    REQUIRE(read.report.fbxFile == fs::path("D:/Projects/Flat/build/building.fbx"));
}

TEST_CASE("readBuiltMaterials reads what the build wrote", "[core][build][materials]") {
    BuildFixture f("run-built-materials");
    plynth::test::requireOk(runBuild(f.request).status);
    const fs::path file = f.projectFolder / "build" / "materials.json";
    REQUIRE(plynth::test::schemaProblem("build-materials.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file))).empty());

    const BuiltMaterialsRead read = readBuiltMaterials(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.materials.size() == 1);
    const BuiltMaterial& m = read.materials[0];
    REQUIRE(m.name == "Wall_Outside");
    REQUIRE(m.material.empty());
    REQUIRE(m.surface == "Wall_Outside");
    REQUIRE(m.baseColor == fs::path(f.projectFolder / "build" / "textures" / "Wall_Outside_base_color.png"));
    REQUIRE(m.opacity.empty());
    REQUIRE(m.occlusion.empty());
    REQUIRE(m.alphaMode == AlphaMode::Opaque);
    REQUIRE(read.objects.size() == 1);
    REQUIRE(read.objects[0].object == "SM_f1_Walls");
    REQUIRE(read.objects[0].slotNames == std::vector<std::string>{"Wall_Outside"});
}

TEST_CASE("readBuiltMaterials reads a library material with an opacity map", "[core][build][materials]") {
    plynth::test::TempDir tmp("built-materials-blend");
    fs::create_directories(tmp.path() / "build");
    nlohmann::json json = {{"format", "plynth-build-materials"},
                           {"version", 1},
                           {"materials",
                            {{{"name", "clear_glass"},
                              {"material", "clear_glass"},
                              {"surface", nullptr},
                              {"maps",
                               {{"base_color", "D:/Lib/a.png"},
                                {"normal", "D:/Lib/b.png"},
                                {"roughness", "D:/Lib/c.png"},
                                {"metallic", "D:/Lib/d.png"},
                                {"opacity", "D:/Lib/e.png"},
                                {"occlusion", nullptr}}},
                              {"alpha_mode", "blend"}}}},
                           {"objects", {{{"name", "SM_f1_Walls"}, {"slots", {"clear_glass"}}}}}};
    std::ofstream(tmp.path() / "build" / "materials.json") << json.dump(2);
    REQUIRE(plynth::test::schemaProblem("build-materials.schema.json", json).empty());
    const BuiltMaterialsRead read = readBuiltMaterials(tmp.path());
    plynth::test::requireOk(read.status);
    REQUIRE(read.materials[0].material == "clear_glass");
    REQUIRE(read.materials[0].surface.empty());
    REQUIRE(read.materials[0].opacity == fs::path("D:/Lib/e.png"));
    REQUIRE(read.materials[0].alphaMode == AlphaMode::Blend);
}

TEST_CASE("readBuiltMaterials says not_found and bad_file", "[core][build][materials]") {
    plynth::test::TempDir tmp("built-materials-bad");
    REQUIRE(readBuiltMaterials(tmp.path()).status.code == ErrorCode::NotFound);

    BuildFixture f("run-built-materials-bad");
    plynth::test::requireOk(runBuild(f.request).status);
    const fs::path file = f.projectFolder / "build" / "materials.json";
    nlohmann::json good = nlohmann::json::parse(plynth::test::readTextFile(file));

    nlohmann::json badAlpha = good;
    badAlpha["materials"][0]["alpha_mode"] = "additive";
    std::ofstream(file, std::ios::trunc) << badAlpha.dump(2);
    const BuiltMaterialsRead alpha = readBuiltMaterials(f.projectFolder);
    REQUIRE(alpha.status.code == ErrorCode::BadFile);
    REQUIRE(alpha.status.where == "/materials/0/alpha_mode");
    REQUIRE(alpha.materials.empty());

    nlohmann::json noMaps = good;
    noMaps["materials"][0].erase("maps");
    std::ofstream(file, std::ios::trunc) << noMaps.dump(2);
    REQUIRE(readBuiltMaterials(f.projectFolder).status.code == ErrorCode::BadFile);

    std::ofstream(file, std::ios::trunc) << "{not json";
    REQUIRE(readBuiltMaterials(f.projectFolder).status.code == ErrorCode::BadFile);
}

TEST_CASE("a report written by milestone 9 reads with the new counts at 0", "[core][build][collision]") {
    plynth::test::TempDir tmp("build-report-m9");
    fs::create_directories(tmp.path() / "build");
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "build" / "build-m9.json", tmp.path() / "build" / "build.json");
    REQUIRE(plynth::test::schemaProblem("build-report.schema.json",
                                        nlohmann::json::parse(plynth::test::readTextFile(tmp.path() / "build" / "build.json")))
                .empty());
    const BuildReportRead read = readBuildReport(tmp.path());
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.counts.materials == 3);
    REQUIRE(read.report.counts.slabs == 0);
    REQUIRE(read.report.counts.stairs == 0);
    REQUIRE(read.report.counts.roofs == 0);
    REQUIRE(read.report.counts.collision == 0);
}

TEST_CASE("a build through the fake writes a report with the slab, stair and roof counts and the preview parts", "[core][build][run]") {
    BuildFixture f("run-m12-counts");
    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(reportProblem(f.reportFile()).empty());
    REQUIRE(report.counts.slabs == 1);
    REQUIRE(report.counts.stairs == 0);
    REQUIRE(report.counts.roofs == 1);
    const nlohmann::json json = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(json["counts"]["roofs"] == 1);
    const fs::path parts = f.projectFolder / "build" / "preview" / "parts.json";
    REQUIRE(fs::exists(parts));
    REQUIRE(plynth::test::schemaProblem("build-preview.schema.json", nlohmann::json::parse(plynth::test::readTextFile(parts))).empty());
    REQUIRE(fs::exists(f.projectFolder / "build" / "preview" / "f1.fbx"));
    REQUIRE(fs::exists(f.projectFolder / "build" / "preview" / "f1_roof.fbx"));
    REQUIRE(fs::exists(f.projectFolder / "build" / "preview" / "f1_slab.fbx"));
    const PreviewPartsRead read = readPreviewParts(f.projectFolder / "build");
    plynth::test::requireOk(read.status);
    REQUIRE(read.parts.size() == 3);
    REQUIRE(read.parts[0].kind == PreviewPartKind::Storey);
    REQUIRE(read.parts[1].kind == PreviewPartKind::Slab);
    REQUIRE(read.parts[2].kind == PreviewPartKind::Roof);
}

TEST_CASE("problemPosition finds a stair and a roof section entry", "[core][build]") {
    Plan plan = makeEmptyPlan();
    plan.floors.push_back(Floor{.id = "f2", .name = "Upper"});
    Stair stair;
    stair.id = "s1";
    stair.x = 1.5;
    stair.y = 2.5;
    plan.floors[0].stairs.push_back(stair);
    RoofSection entry;
    entry.id = "rf1";
    entry.floor = "f2";
    entry.x = 3.0;
    entry.y = 4.0;
    plan.roofSections.push_back(entry);
    BuildProblem p;
    p.object = "s1";
    REQUIRE(problemPosition(plan, p) == Vec2{1.5, 2.5});
    p.object = "rf1";
    REQUIRE(problemPosition(plan, p) == Vec2{3.0, 4.0});
}

TEST_CASE("the build report keeps counts.collision", "[core][build][collision][run]") {
    BuildFixture f("run-collision-count");
    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.counts.collision > 0);
    REQUIRE(reportProblem(f.reportFile()).empty());
    const BuildReportRead read = readBuildReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.counts.collision == report.counts.collision);
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["counts"]["collision"] == report.counts.collision);
}

TEST_CASE("readBuiltMaterials reads the asset of a furniture material", "[core][build][materials]") {
    plynth::test::TempDir tmp("built-materials-asset");
    fs::create_directories(tmp.path() / "build");
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "build" / "materials-asset.json", tmp.path() / "build" / "materials.json");
    REQUIRE(plynth::test::schemaProblem("build-materials.schema.json",
                                        nlohmann::json::parse(plynth::test::readTextFile(tmp.path() / "build" / "materials.json")))
                .empty());
    const BuiltMaterialsRead read = readBuiltMaterials(tmp.path());
    plynth::test::requireOk(read.status);
    REQUIRE(read.materials.size() == 2);
    REQUIRE(read.materials[0].asset.empty());
    REQUIRE(read.materials[0].material == "white_paint");
    REQUIRE(read.materials[1].asset == "sofa");
    REQUIRE(read.materials[1].name == "Glass.001");
    REQUIRE(read.materials[1].material.empty());
    REQUIRE(read.materials[1].surface.empty());
    REQUIRE(read.materials[1].alphaMode == AlphaMode::Blend);
}

namespace {

// Writes a generator file into the library that needs the given add-ons.
void writeTestGenerator(const fs::path& folder, const std::string& id, const std::string& addons)
{
    fs::create_directories(folder);
    std::ofstream(folder / (id + ".py"), std::ios::binary)
        << "\"\"\"plynth-generator\n"
        << R"({"format": "plynth-generator", "version": 1, "name": "Test part", "description": "A part for tests.", )"
        << R"("addons": )" << addons
        << R"(, "size": {"width": 0.5, "depth": 0.5, "height": 1.0}, )"
        << R"("params": [{"id": "amount", "label": "Amount", "type": "number", "default": 1.0}]})"
        << "\n\"\"\"\n\ndef generate(gen):\n    return []\n";
}

// Places one item of a library generator on the first floor.
void placeGenerator(BuildRequest& request, const std::string& generator, const std::string& id)
{
    const GeneratorLibrary library = loadGeneratorFolder(request.library->generators);
    const GeneratorInfo* info = library.find(generator);
    REQUIRE(info != nullptr);
    const GeneratorPlacement placed = makeGeneratorUse(*info, defaultGeneratorParams(*info, 3.0));
    plynth::test::requireOk(placed.status);
    Item item = buildItem(id, "", 1.0, 1.0, placed.size);
    item.generator = placed.use;
    request.project.plan.floors[0].items.push_back(item);
}

// The tasks of the jobs the fake Blender was given, in order.
std::vector<std::string> fakeTasks(const BuildRequest& request)
{
    std::vector<std::string> tasks;
    std::ifstream in(request.blender.executable.parent_path() / "fake_jobs.log", std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) tasks.push_back(nlohmann::json::parse(line).at("task").get<std::string>());
    }
    return tasks;
}

// The generator_unavailable problems of a report.
std::vector<BuildProblem> unavailableProblems(const BuildReport& report)
{
    std::vector<BuildProblem> found;
    for (const BuildProblem& p : report.problems) {
        if (p.code == "generator_unavailable") found.push_back(p);
    }
    return found;
}

}

TEST_CASE("runBuild puts the library's generators into the job and counts them", "[core][build][run][generators]") {
    BuildFixture f("run-generators");
    writeTestGenerator(f.request.library->generators, "post", "[]");
    placeGenerator(f.request, "post", "g1");
    f.request.run.factoryStartup = false;

    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    const nlohmann::json job = nlohmann::json::parse(plynth::test::readTextFile(f.projectFolder / "build" / "job.json"));
    REQUIRE(job["params"]["generators"].size() == 1);
    REQUIRE(job["params"]["generators"][0]["id"] == "post");
    REQUIRE(job["params"]["floors"][0]["generator_items"].size() == 1);
    REQUIRE(job["params"]["floors"][0]["generator_items"][0]["id"] == "g1");
    REQUIRE(report.counts.generators == 1);
    REQUIRE(fakeTasks(f.request) == std::vector<std::string>{"build"});
    // A request for normal startup still builds with factory startup.
    std::ifstream args(f.request.blender.executable.parent_path() / "fake_args.log", std::ios::binary);
    int jobs = 0;
    for (std::string line; std::getline(args, line);) {
        if (line.find("--version") != std::string::npos) continue;
        ++jobs;
        REQUIRE(line.find("--factory-startup") != std::string::npos);
    }
    REQUIRE(jobs == 1);

    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["counts"]["generators"] == 1);
    const BuildReportRead read = readBuildReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.counts.generators == 1);
}

TEST_CASE("the build report writes counts.generators and reads only a count", "[core][build][generators]") {
    BuildReport report;
    report.jobId = "build-00000001";
    report.startedAt = sessionNow();
    report.planFingerprint = "0123456789abcdef";
    report.blenderVersion = "5.2.0";
    report.counts.generators = 4;
    nlohmann::json json = nlohmann::json::parse(buildReportToJson(report).dump());
    REQUIRE(json["counts"]["generators"] == 4);
    REQUIRE(plynth::test::schemaProblem("build-report.schema.json", json).empty());

    plynth::test::TempDir tmp("build-report-generators");
    const fs::path file = tmp.path() / "build" / "build.json";
    fs::create_directories(file.parent_path());
    const auto write = [&] { std::ofstream(file, std::ios::binary | std::ios::trunc) << json.dump(); };
    write();
    BuildReportRead read = readBuildReport(tmp.path());
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.counts.generators == 4);

    for (const nlohmann::json& bad : {nlohmann::json(-1), nlohmann::json(2.5), nlohmann::json("3"), nlohmann::json(nullptr)}) {
        json["counts"]["generators"] = bad;
        write();
        read = readBuildReport(tmp.path());
        CHECK(read.status.code == ErrorCode::BadFile);
        CHECK(read.status.where == "/counts/generators");
    }

    json["counts"].erase("generators");
    write();
    read = readBuildReport(tmp.path());
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.counts.generators == 0);
}

TEST_CASE("a build that would copy nothing never asks Blender for its add-ons", "[core][build][run][generators]") {
    BuildFixture f("run-generators-no-probe");
    writeTestGenerator(f.request.library->generators, "plain", "[]");
    writeTestGenerator(f.request.library->generators, "needs", R"(["plynth_test_addon"])");
    std::size_t warnings = 0;

    SECTION("no source")
    {
        placeGenerator(f.request, "needs", "g1");
    }
    SECTION("the extensions folder as the source and only plain generators")
    {
        placeGenerator(f.request, "plain", "g1");
        f.request.addonSource = blenderUserFolders(f.request.blender).extensions;
        REQUIRE_FALSE(f.request.addonSource.empty());
    }
    SECTION("another folder as the source")
    {
        placeGenerator(f.request, "needs", "g1");
        f.request.addonSource = f.lib.tmp.path() / "elsewhere";
        warnings = 1;
    }

    const BuildReport report = runBuild(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(fakeTasks(f.request) == std::vector<std::string>{"build"});
    REQUIRE(report.counts.generators == 1);
    const std::vector<BuildProblem> found = unavailableProblems(report);
    REQUIRE(found.size() == warnings);
    for (const BuildProblem& p : found) {
        REQUIRE(p.severity == BuildSeverity::Warning);
        REQUIRE(p.object.empty());
        REQUIRE(p.step == "check");
    }
    const nlohmann::json job = nlohmann::json::parse(plynth::test::readTextFile(f.projectFolder / "build" / "job.json"));
    REQUIRE(job["params"]["floors"][0]["generator_items"].size() == 1);
}
