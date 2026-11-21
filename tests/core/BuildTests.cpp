#include "BuildFixtures.h"
#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
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
    REQUIRE(fs::exists(f.projectFolder / "build" / "job.json"));
    REQUIRE(fs::exists(f.projectFolder / "build" / "result.json"));
    REQUIRE(fs::exists(f.projectFolder / "build" / "blender.log"));
    REQUIRE(report.logFile == f.projectFolder / "build" / "blender.log");
    REQUIRE_FALSE(fs::exists(f.lockFile()));

    REQUIRE(report.counts == BuildCounts{1, 1, 1, 1, 2});
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
    REQUIRE_FALSE(fs::exists(f.projectFolder / "build" / "building.fbx"));
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
