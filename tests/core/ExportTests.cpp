#include "CatalogSupport.h"
#include "MaterialCatalogSupport.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/Export.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/Exporter.h>
#include <plynth/core/ExportJob.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// A built project folder (a stand in building.blend), a fake Blender and a request to export it.
struct ExportFixture {
    explicit ExportFixture(const std::string& tag, const std::string& mode = "") : tmp(tag)
    {
        projectFolder = tmp.path() / "Project";
        fs::create_directories(projectFolder / "build");
        std::ofstream(projectFolder / "build" / "building.blend", std::ios::binary) << "FAKEBLEND";
        blenderFolder = tmp.path() / "blender";
        const fs::path exe = makeFakeBlender(blenderFolder, "5.2.0", mode);
        request.projectFolder = projectFolder;
        request.project = squareProjectWithWalls();
        request.project.settings.name = "Square";
        request.blender = inspectBlender(exe);
        request.run.scriptsFolder = makeFakeScripts(tmp.path() / "scripts");
    }

    fs::path lockFile() const { return projectFolder / "build" / "build.lock"; }
    fs::path exportDir() const { return projectFolder / "export"; }
    fs::path reportFile() const { return exportDir() / "export_report.json"; }
    fs::path staging() const { return exportDir() / ".staging"; }

    void setMode(const std::string& mode) const { std::ofstream(blenderFolder / "fake_mode.txt") << mode; }

    // The params of the last job the fake Blender got.
    Json lastJobParams() const
    {
        std::ifstream in(blenderFolder / "fake_jobs.log");
        std::string line, last;
        while (std::getline(in, line)) {
            if (!line.empty()) last = line;
        }
        return Json::parse(last)["params"];
    }

    // The names of the entries in the export folder.
    std::set<std::string> exportEntries() const
    {
        std::set<std::string> names;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(exportDir(), ec)) names.insert(entry.path().filename().string());
        return names;
    }

    // Writes build/build.json as a build of a project would.
    void writeBuildReport(const std::string& fingerprint) const
    {
        BuildReport build;
        build.jobId = "build-00000001";
        build.startedAt = sessionNow();
        build.planFingerprint = fingerprint;
        build.blenderVersion = "5.2.0";
        BuildProblem problem;
        problem.code = "cut_failed";
        problem.severity = BuildSeverity::Error;
        problem.message = "The cut for opening o1 failed.";
        problem.floor = "f1";
        problem.object = "o1";
        problem.step = "openings";
        build.problems.push_back(problem);
        std::ofstream(projectFolder / "build" / "build.json", std::ios::binary) << buildReportToJson(build).dump(2);
    }

    TempDir tmp;
    fs::path projectFolder;
    fs::path blenderFolder;
    ExportRequest request;
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
    return plynth::test::schemaProblem("export-report.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file)));
}

}

TEST_CASE("exportFileStem makes a safe file name", "[core][export]") {
    REQUIRE(exportFileStem("My Flat 2") == "My_Flat_2");
    REQUIRE(exportFileStem("  ") == "Building");
    REQUIRE(exportFileStem("") == "Building");
    REQUIRE(exportFileStem("a/b\\c:d") == "a_b_c_d");
    REQUIRE(exportFileStem("  Flat   -  one!! ") == "Flat_-_one");
    REQUIRE(exportFileStem("Flat_-ok") == "Flat_-ok");
    REQUIRE(exportFileStem("Wohnung \xC3\xBC") == "Wohnung");
    REQUIRE(exportFileStem(std::string(100, 'a')) == std::string(64, 'a'));
    REQUIRE(exportFileStem(std::string(63, 'a') + " b") == std::string(63, 'a'));
}

TEST_CASE("the single and grouped exporters are registered", "[core][export]") {
    const Exporter* single = findExporter("single");
    REQUIRE(single);
    REQUIRE(single->id() == kSingleExporterId);
    REQUIRE(single->name() == "Single FBX");
    REQUIRE_FALSE(single->description().empty());
    REQUIRE(findExporter("grouped"));
    REQUIRE(findExporter("grouped")->id() == kGroupedExporterId);

    Project project = makeNewProject();
    project.settings.name = "My Flat 2";
    std::vector<ExportGroup> groups;
    plynth::test::requireOk(single->groups(project, groups));
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0] == ExportGroup{"building", "My_Flat_2.fbx", {}, {}});

    bool found = false;
    for (const Exporter* e : allExporters()) found = found || e->id() == "single";
    REQUIRE(found);
}

TEST_CASE("an exporter with a taken or badly formed id is dropped", "[core][export]") {
    class Odd final : public Exporter {
    public:
        explicit Odd(std::string id) : id_(std::move(id)) {}
        std::string_view id() const override { return id_; }
        std::string_view name() const override { return "Odd"; }
        std::string_view description() const override { return "Odd."; }
        Status groups(const Project&, std::vector<ExportGroup>&) const override { return {}; }

    private:
        std::string id_;
    };
    REQUIRE_FALSE(registerExporter(std::make_unique<Odd>("single")));
    REQUIRE_FALSE(registerExporter(std::make_unique<Odd>("Upper")));
    REQUIRE_FALSE(registerExporter(std::make_unique<Odd>("")));
    REQUIRE_FALSE(registerExporter(nullptr));
}

TEST_CASE("runExport through the fake Blender exports and writes the report", "[core][export][run]") {
    ExportFixture f("run-ok");
    std::vector<ExportStage> stages;
    int workerSteps = 0;
    const ExportReport report = runExport(f.request, [&](const ExportProgress& p) {
        if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
        if (p.stage == ExportStage::Blender && !p.blender.step.empty()) {
            ++workerSteps;
            REQUIRE(p.blender.index == workerSteps);
        }
    });
    plynth::test::requireOk(report.status);
    REQUIRE(stages == std::vector<ExportStage>{ExportStage::Preparing, ExportStage::Blender, ExportStage::Finishing, ExportStage::Done});
    REQUIRE(workerSteps == 8);

    REQUIRE(report.exported);
    REQUIRE_FALSE(report.checkedOnly);
    REQUIRE_FALSE(report.forced);
    REQUIRE(report.overridden.empty());
    REQUIRE(report.exporter == "single");
    REQUIRE(report.jobId.rfind("export-", 0) == 0);
    REQUIRE(report.blenderVersion == "5.2.0");
    REQUIRE(report.planFingerprint == planFingerprint(f.request.project));
    REQUIRE(report.buildFingerprint.empty());
    REQUIRE(report.files == std::vector<fs::path>{f.exportDir() / "Square.fbx"});
    REQUIRE(report.fileOutputs.size() == 1);
    REQUIRE(report.fileOutputs[0].group == "building");
    REQUIRE(report.texturesFolder == f.exportDir() / "Textures");
    REQUIRE(report.textureCount == 1);
    REQUIRE(report.reportFile == f.reportFile());
    REQUIRE(report.logFile == f.projectFolder / "build" / "export_work" / "blender.log");
    REQUIRE(report.objects == 12);
    REQUIRE(report.triangles == 144);
    REQUIRE(report.collisionObjects == 3);
    REQUIRE(report.checks.size() == 3);
    REQUIRE(report.passed());
    REQUIRE(report.failCount() == 0);
    REQUIRE(report.warningCount() == 0);
    REQUIRE(report.steps.size() == 9);
    REQUIRE(report.steps[0].step == "prepare");
    REQUIRE(report.steps[1].step == "open_blend");
    REQUIRE(report.steps[6].step == "finish_objects");
    REQUIRE(report.steps[8].step == "file_checks");

    REQUIRE(fs::is_regular_file(f.exportDir() / "Square.fbx"));
    REQUIRE(fs::is_regular_file(f.exportDir() / "Textures" / "T_Fake_BaseColor.png"));
    REQUIRE(fs::is_regular_file(f.reportFile()));
    REQUIRE_FALSE(fs::exists(f.staging()));
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE(fs::is_regular_file(f.projectFolder / "build" / "export_work" / "job.json"));
    REQUIRE(fs::is_regular_file(f.projectFolder / "build" / "export_work" / "result.json"));
    REQUIRE(fs::is_regular_file(f.projectFolder / "build" / "export_work" / "blender.log"));
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Textures", "export_report.json"});

    INFO(reportProblem(f.reportFile()));
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["ok"] == true);
    REQUIRE(written["exported"] == true);
    REQUIRE(written["outputs"]["files"][0]["file"] == "Square.fbx");
    REQUIRE(written["outputs"]["files"][0]["triangles"] == 144);
    REQUIRE(written["outputs"]["texture_count"] == 1);
    REQUIRE(written["build_fingerprint"].is_null());
    REQUIRE(written["quality"]["passed"] == true);
    REQUIRE(written["profile"] == "plain");
    REQUIRE(report.profile == ExportProfile::Plain);
    REQUIRE(f.lastJobParams()["profile"] == "plain");
    REQUIRE(f.lastJobParams()["kit"].is_null());

    // The project folder's plan and settings are never written.
    REQUIRE_FALSE(fs::exists(f.projectFolder / "plan.json"));
    REQUIRE_FALSE(fs::exists(f.projectFolder / "project.json"));
}

TEST_CASE("a second export replaces the files and removes an old fbx the report listed", "[core][export][run]") {
    ExportFixture f("run-twice");
    plynth::test::requireOk(runExport(f.request).status);
    std::ofstream(f.exportDir() / "Textures" / "T_Old.png") << "OLD";
    std::ofstream(f.exportDir() / "Notes.txt") << "mine";

    f.request.project.settings.name = "Other";
    const ExportReport second = runExport(f.request);
    plynth::test::requireOk(second.status);
    REQUIRE(fs::exists(f.exportDir() / "Other.fbx"));
    REQUIRE_FALSE(fs::exists(f.exportDir() / "Square.fbx"));
    REQUIRE_FALSE(fs::exists(f.exportDir() / "Textures" / "T_Old.png"));
    REQUIRE(fs::exists(f.exportDir() / "Textures" / "T_Fake_BaseColor.png"));
    REQUIRE(fs::exists(f.exportDir() / "Notes.txt"));
    REQUIRE(f.exportEntries() == std::set<std::string>{"Other.fbx", "Textures", "Notes.txt", "export_report.json"});
    REQUIRE(reportProblem(f.reportFile()).empty());

    // The same name again replaces the file in place.
    std::ofstream(f.exportDir() / "Other.fbx") << "STALE";
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(plynth::test::readTextFile(f.exportDir() / "Other.fbx") == "FAKEFBX");
}

TEST_CASE("a failed check blocks the export and leaves the last one alone", "[core][export][run]") {
    ExportFixture f("run-blocked");
    plynth::test::requireOk(runExport(f.request).status);
    std::ofstream(f.exportDir() / "Square.fbx") << "LASTGOOD";

    f.setMode("quality_fail");
    const ExportReport report = runExport(f.request);
    REQUIRE(report.status.code == ErrorCode::QualityFailed);
    REQUIRE(report.status.message.find("Naming") != std::string::npos);
    REQUIRE_FALSE(report.exported);
    REQUIRE(report.files == std::vector<fs::path>{f.exportDir() / "Square.fbx"});
    REQUIRE(report.texturesFolder.empty());
    REQUIRE_FALSE(report.passed());
    REQUIRE(report.failCount() == 1);
    REQUIRE(report.checks[1].id == "naming");
    REQUIRE(report.checks[1].findings.size() == 1);
    REQUIRE(report.checks[1].findings[0].planId == "w1");

    REQUIRE(plynth::test::readTextFile(f.exportDir() / "Square.fbx") == "LASTGOOD");
    REQUIRE(fs::exists(f.exportDir() / "Textures" / "T_Fake_BaseColor.png"));
    REQUIRE_FALSE(fs::exists(f.staging()));
    REQUIRE_FALSE(fs::exists(f.lockFile()));

    INFO(reportProblem(f.reportFile()));
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["ok"] == false);
    REQUIRE(written["exported"] == false);
    REQUIRE(written["error"]["code"] == "quality_failed");
    REQUIRE(written["quality"]["passed"] == false);
    REQUIRE(written["quality"]["fail_count"] == 1);
    REQUIRE(written["outputs"]["files"].size() == 1);
    REQUIRE(written["outputs"]["files"][0]["file"] == "Square.fbx");
}

TEST_CASE("a blocked or check-only export keeps the old file list so a rename cleans up later", "[core][export][run]") {
    ExportFixture f("run-rename-blocked");
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(fs::exists(f.exportDir() / "Square.fbx"));

    f.request.project.settings.name = "Other";
    f.setMode("quality_fail");
    REQUIRE(runExport(f.request).status.code == ErrorCode::QualityFailed);
    ExportReportRead read = readExportReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.files == std::vector<fs::path>{f.exportDir() / "Square.fbx"});

    f.request.settings.write = false;
    f.setMode("");
    plynth::test::requireOk(runExport(f.request).status);
    read = readExportReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.files == std::vector<fs::path>{f.exportDir() / "Square.fbx"});

    f.request.settings.write = true;
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(fs::exists(f.exportDir() / "Other.fbx"));
    REQUIRE_FALSE(fs::exists(f.exportDir() / "Square.fbx"));
}

TEST_CASE("force exports past a failed check and names it", "[core][export][run]") {
    ExportFixture f("run-force", "quality_fail");
    f.request.settings.force = true;
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.exported);
    REQUIRE(report.forced);
    REQUIRE(report.overridden == std::vector<std::string>{"naming"});
    REQUIRE_FALSE(report.passed());
    REQUIRE(fs::exists(f.exportDir() / "Square.fbx"));
    REQUIRE(f.lastJobParams()["force"] == true);

    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["ok"] == true);
    REQUIRE(written["forced"] == true);
    REQUIRE(written["overridden"] == nlohmann::json::array({"naming"}));
}

TEST_CASE("a warning does not block the export", "[core][export][run]") {
    ExportFixture f("run-warn", "quality_warn");
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.exported);
    REQUIRE(report.passed());
    REQUIRE(report.warningCount() == 1);
    REQUIRE(report.overridden.empty());
}

TEST_CASE("write false only checks and moves nothing", "[core][export][run]") {
    ExportFixture f("run-check");
    f.request.settings.write = false;
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.checkedOnly);
    REQUIRE_FALSE(report.exported);
    REQUIRE(report.files.empty());
    REQUIRE(report.checks.size() == 3);
    REQUIRE(report.checks[2].status == QualityStatus::Skipped);
    REQUIRE(f.exportEntries() == std::set<std::string>{"export_report.json"});
    REQUIRE(f.lastJobParams()["write"] == false);
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["ok"] == true);
    REQUIRE(written["checked_only"] == true);
    REQUIRE(written["exported"] == false);
}

TEST_CASE("the job carries what the last build says", "[core][export][run]") {
    ExportFixture f("run-job");
    plynth::test::requireOk(runExport(f.request).status);
    Json build = f.lastJobParams()["build"];
    REQUIRE(build["report_found"] == false);
    REQUIRE(build["plan_changed"] == true);
    REQUIRE(f.lastJobParams()["materials"].empty());
    REQUIRE(f.lastJobParams()["project"]["name"] == "Square");
    REQUIRE(f.lastJobParams()["groups"][0]["file"] == "Square.fbx");
    REQUIRE(f.lastJobParams()["output_folder"] == pathToUtf8(f.staging()));
    REQUIRE(f.lastJobParams()["blend_file"] == pathToUtf8(f.projectFolder / "build" / "building.blend"));

    const std::string current = planFingerprint(f.request.project);
    f.writeBuildReport(current);
    const ExportReport same = runExport(f.request);
    plynth::test::requireOk(same.status);
    build = f.lastJobParams()["build"];
    REQUIRE(build["report_found"] == true);
    REQUIRE(build["plan_changed"] == false);
    REQUIRE(build["problems"].size() == 1);
    REQUIRE(build["problems"][0]["code"] == "cut_failed");
    REQUIRE(same.buildFingerprint == current);

    f.writeBuildReport("0123456789abcdef");
    const ExportReport changed = runExport(f.request);
    plynth::test::requireOk(changed.status);
    REQUIRE(f.lastJobParams()["build"]["plan_changed"] == true);
    REQUIRE(changed.buildFingerprint == "0123456789abcdef");
    REQUIRE(reportProblem(f.reportFile()).empty());
}

TEST_CASE("the expected bounds come from the plan that was built", "[core][export][run]") {
    ExportFixture f("run-built-bounds");
    const Project built = f.request.project;
    const std::optional<ExportBounds> builtBounds = expectedWallBounds(built);
    REQUIRE(builtBounds.has_value());

    std::ofstream(f.projectFolder / "build" / "built-plan.json", std::ios::binary) << Json{{"plan_fingerprint", planFingerprint(built)}, {"plan", planToJson(built.plan)}}.dump();
    f.writeBuildReport(planFingerprint(built));

    f.request.project.plan.floors[0].points[0].x -= 0.1;
    REQUIRE(planFingerprint(f.request.project) != planFingerprint(built));
    REQUIRE(expectedWallBounds(f.request.project) != builtBounds);

    plynth::test::requireOk(runExport(f.request).status);
    const Json params = f.lastJobParams();
    REQUIRE(params["build"]["plan_changed"] == true);
    REQUIRE(params["expected_bounds"]["min"][0].get<double>() == builtBounds->min.x);
    REQUIRE(params["expected_bounds"]["max"][0].get<double>() == builtBounds->max.x);
}

TEST_CASE("a failed build after a plan change does not make the build current", "[core][export][run]") {
    ExportFixture f("run-failed-build");
    const Project built = f.request.project;
    std::ofstream(f.projectFolder / "build" / "built-plan.json", std::ios::binary)
        << Json{{"plan_fingerprint", planFingerprint(built)}, {"plan", planToJson(built.plan)}}.dump();
    f.request.project.plan.floors[0].points[0].x -= 0.1;

    BuildReport failed;
    failed.jobId = "build-00000002";
    failed.startedAt = sessionNow();
    failed.planFingerprint = planFingerprint(f.request.project);
    failed.status = fail(ErrorCode::BlenderFailed, "The build failed.");
    failed.errorCode = "save_failed";
    std::ofstream(f.projectFolder / "build" / "build.json", std::ios::binary) << buildReportToJson(failed).dump(2);

    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(f.lastJobParams()["build"]["report_found"] == true);
    REQUIRE(f.lastJobParams()["build"]["plan_changed"] == true);
    REQUIRE(report.buildFingerprint == planFingerprint(built));
}

TEST_CASE("the job carries the materials of the build", "[core][export][run]") {
    ExportFixture f("run-materials");
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "export" / "materials.json", f.projectFolder / "build" / "materials.json");
    plynth::test::requireOk(runExport(f.request).status);
    const Json materials = f.lastJobParams()["materials"];
    REQUIRE(materials.size() == 3);
    REQUIRE(materials[0]["name"] == "Living_Floor");
}

TEST_CASE("runExport refuses before doing anything", "[core][export][run]") {
    ExportFixture f("run-refuse");
    ExportRequest missing = f.request;
    missing.projectFolder = f.tmp.path() / "nowhere";
    REQUIRE(runExport(missing).status.code == ErrorCode::NotFound);

    fs::remove(f.projectFolder / "build" / "building.blend");
    const ExportReport noBuild = runExport(f.request);
    REQUIRE(noBuild.status.code == ErrorCode::NotFound);
    REQUIRE(noBuild.status.message == "Build the project first.");
    REQUIRE_FALSE(fs::exists(f.exportDir()));

    std::ofstream(f.projectFolder / "build" / "building.blend") << "FAKEBLEND";
    ExportRequest noBlender = f.request;
    noBlender.blender = BlenderInstall{};
    REQUIRE(runExport(noBlender).status.code == ErrorCode::BlenderNotFound);
    REQUIRE_FALSE(fs::exists(f.exportDir()));
}

TEST_CASE("a fresh build lock is in use and left alone", "[core][export][run]") {
    ExportFixture f("run-locked");
    writeLock(f.lockFile(), currentProcess(), epochNow());
    const ExportReport report = runExport(f.request);
    REQUIRE(report.status.code == ErrorCode::InUse);
    REQUIRE(fs::exists(f.lockFile()));
    REQUIRE_FALSE(fs::exists(f.exportDir()));
    fs::remove(f.lockFile());

    writeLock(f.lockFile(), ProcessIdentity{999999, 1}, epochNow() - 100000);
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE_FALSE(fs::exists(f.lockFile()));
}

TEST_CASE("stopping an export gives cancelled and removes the staging folder and the lock", "[core][export][run]") {
    ExportFixture f("run-stop", "hang");
    std::stop_source source;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        source.request_stop();
    });
    const ExportReport report = runExport(f.request, {}, source.get_token());
    stopper.join();
    REQUIRE(report.status.code == ErrorCode::Cancelled);
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE_FALSE(fs::exists(f.staging()));
    REQUIRE_FALSE(fs::exists(f.reportFile()));
}

TEST_CASE("a failed worker is blender_failed with its code, and the report is written", "[core][export][run]") {
    ExportFixture f("run-fail", "fail");
    const ExportReport report = runExport(f.request);
    REQUIRE(report.status.code == ErrorCode::BlenderFailed);
    REQUIRE(report.errorCode == "export_failed");
    REQUIRE(report.status.message.find("Fake export failure.") != std::string::npos);
    REQUIRE(report.status.message.find("blender.log") != std::string::npos);
    REQUIRE_FALSE(report.exported);
    REQUIRE_FALSE(fs::exists(f.staging()));
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["error"]["code"] == "blender_failed");
    REQUIRE(written["error"]["worker_code"] == "export_failed");
    REQUIRE(written["quality"]["checks"].empty());
}

TEST_CASE("a result that is not valid is blender_failed", "[core][export][run]") {
    ExportFixture f("run-bad", "bad_result");
    const ExportReport report = runExport(f.request);
    REQUIRE(report.status.code == ErrorCode::BlenderFailed);
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE(reportProblem(f.reportFile()).empty());
}

TEST_CASE("readExportReport gives back the report that was written", "[core][export][run]") {
    ExportFixture f("run-read", "quality_fail");
    REQUIRE(readExportReport(f.projectFolder).status.code == ErrorCode::NotFound);

    f.request.settings.force = true;
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    const ExportReportRead read = readExportReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(exportReportToJson(read.report) == exportReportToJson(report));
    REQUIRE(read.report.status.code == report.status.code);
    REQUIRE(read.report.jobId == report.jobId);
    REQUIRE(read.report.startedAt == report.startedAt);
    REQUIRE(read.report.exported);
    REQUIRE(read.report.forced);
    REQUIRE(read.report.overridden == report.overridden);
    REQUIRE(read.report.files == report.files);
    REQUIRE(read.report.fileOutputs == report.fileOutputs);
    REQUIRE(read.report.texturesFolder == report.texturesFolder);
    REQUIRE(read.report.reportFile == report.reportFile);
    REQUIRE(read.report.logFile == report.logFile);
    REQUIRE(read.report.checks == report.checks);
    REQUIRE(read.report.steps == report.steps);
    REQUIRE(read.report.objects == report.objects);

    f.setMode("quality_fail");
    f.request.settings.force = false;
    const ExportReport blocked = runExport(f.request);
    const ExportReportRead readBlocked = readExportReport(f.projectFolder);
    plynth::test::requireOk(readBlocked.status);
    REQUIRE(readBlocked.report.status.code == ErrorCode::QualityFailed);
    REQUIRE(readBlocked.report.status.message == blocked.status.message);
    REQUIRE(exportReportToJson(readBlocked.report) == exportReportToJson(blocked));
}

TEST_CASE("readExportReport refuses a file that is not a report", "[core][export][run]") {
    ExportFixture f("run-read-bad");
    fs::create_directories(f.exportDir());
    std::ofstream(f.reportFile()) << "[1, 2]";
    REQUIRE(readExportReport(f.projectFolder).status.code == ErrorCode::BadFile);

    std::ofstream(f.reportFile(), std::ios::trunc) << R"({"format": "plynth-export-report", "version": 1, "ok": true})";
    const ExportReportRead missing = readExportReport(f.projectFolder);
    REQUIRE(missing.status.code == ErrorCode::BadFile);
    REQUIRE(missing.status.where == "/exported");
}

namespace {

// Makes the fixture's project a grouped one: living room r1 and bedroom r2 with an item each, and a door.
void makeGrouped(ExportFixture& f)
{
    f.request.project.settings.exportMode = ExportMode::Grouped;
    Floor& floor = f.request.project.plan.floors[0];
    Room r1;
    r1.id = "r1";
    r1.type = "living";
    r1.walls = {"w1", "w2", "w3", "w4"};
    Room r2;
    r2.id = "r2";
    r2.type = "bedroom";
    r2.walls = {"w1", "w2"};
    floor.rooms = {r1, r2};
    Opening door = doorOn("w1", 2.0);
    door.id = "o1";
    floor.openings = {door};
    Item sofa;
    sofa.id = "i1";
    sofa.asset = "sofa";
    sofa.room = "r1";
    sofa.size = {1.0, 1.0, 1.0};
    Item bed = sofa;
    bed.id = "i2";
    bed.asset = "bed";
    bed.room = "r2";
    floor.items = {sofa, bed};
}

// The job the fake Blender got last, as the whole file.
Json lastJob(const ExportFixture& f)
{
    return Json::parse(plynth::test::readTextFile(f.projectFolder / "build" / "export_work" / "job.json"));
}

// Checks a job against the job schema.
std::string jobSchemaProblem(const Json& job)
{
    return plynth::test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump()));
}

}

TEST_CASE("a grouped project exports one file per group", "[core][export][run]") {
    ExportFixture f("run-grouped");
    makeGrouped(f);
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.exporter == "grouped");
    REQUIRE(report.exported);
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square_Walls.fbx", "Square_Floors.fbx", "Square_Furniture_Living.fbx",
                                                       "Square_Furniture_Bedroom.fbx", "Textures", "export_report.json"});
    REQUIRE(report.fileOutputs.size() == 4);
    REQUIRE(report.fileOutputs[0].group == "structure");
    REQUIRE(report.fileOutputs[0].name == "Walls");
    REQUIRE(report.fileOutputs[3].name == "Furniture Bedroom");
    REQUIRE(report.emptyGroups.empty());
    REQUIRE(report.groupNotes.empty());

    INFO(reportProblem(f.reportFile()));
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["outputs"]["files"][2]["name"] == "Furniture Living");
    REQUIRE(written["groups"]["empty"].empty());

    const Json job = lastJob(f);
    INFO(jobSchemaProblem(job));
    REQUIRE(jobSchemaProblem(job).empty());
    const Json& groups = job["params"]["groups"];
    REQUIRE(groups.size() == 4);
    REQUIRE(groups[0]["id"] == "structure");
    REQUIRE(groups[0]["name"] == "Walls");
    REQUIRE(groups[0]["rest"] == true);
    REQUIRE(groups[0]["members"].size() == 3);
    REQUIRE(groups[0]["members"][0] == Json{{"kind", "walls"}, {"id", "f1"}});
    REQUIRE(std::find(groups[1]["members"].begin(), groups[1]["members"].end(), Json{{"kind", "guard"}, {"id", nullptr}}) !=
            groups[1]["members"].end());
    REQUIRE(groups[2]["rest"] == false);
    REQUIRE(job["params"]["kit"].is_null());

    const ExportReportRead read = readExportReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.fileOutputs == report.fileOutputs);
}

TEST_CASE("saved group edits rename the file and the next export removes the old one", "[core][export][run]") {
    ExportFixture f("run-grouped-rename");
    makeGrouped(f);
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(fs::exists(f.exportDir() / "Square_Furniture_Living.fbx"));

    ExportOptions options;
    options.groups.renames = {{"furniture_r1", "Lounge props"}};
    options.groups.merges = {{"furniture_r2", "structure"}};
    plynth::test::requireOk(writeExportOptions(f.projectFolder, options));
    const ExportReport second = runExport(f.request);
    plynth::test::requireOk(second.status);
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square_Walls.fbx", "Square_Floors.fbx", "Square_Lounge_props.fbx", "Textures",
                                                       "export_report.json"});
    REQUIRE(second.fileOutputs[2].name == "Lounge props");
    REQUIRE(lastJob(f)["params"]["groups"][0]["members"].size() == 5);
    REQUIRE(reportProblem(f.reportFile()).empty());
    REQUIRE(second.groupNotes.empty());
}

TEST_CASE("a saved edit that no longer applies is in the report notes", "[core][export][run]") {
    ExportFixture f("run-grouped-notes");
    makeGrouped(f);
    ExportOptions options;
    options.groups.renames = {{"furniture_r9", "Gone"}};
    plynth::test::requireOk(writeExportOptions(f.projectFolder, options));
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.groupNotes.size() == 1);
    REQUIRE(report.groupNotes[0].find("furniture_r9") != std::string::npos);
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["groups"]["notes"].size() == 1);
    REQUIRE(readExportReport(f.projectFolder).report.groupNotes == report.groupNotes);
}

TEST_CASE("a group that gets no object is listed and not written", "[core][export][run]") {
    ExportFixture f("run-grouped-empty", "empty_group");
    makeGrouped(f);
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.emptyGroups == std::vector<std::string>{"furniture_r2"});
    REQUIRE(report.fileOutputs.size() == 3);
    REQUIRE_FALSE(fs::exists(f.exportDir() / "Square_Furniture_Bedroom.fbx"));
    REQUIRE(reportProblem(f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["groups"]["empty"] == nlohmann::json::array({"furniture_r2"}));
    REQUIRE(readExportReport(f.projectFolder).report.emptyGroups == report.emptyGroups);
}

TEST_CASE("a bad options file stops a grouped export before anything is written", "[core][export][run]") {
    ExportFixture f("run-grouped-bad");
    makeGrouped(f);
    std::ofstream(f.projectFolder / "export-options.json", std::ios::binary) << R"({"format": "plynth-export-options", "version": 1, "x": 1})";
    const ExportReport report = runExport(f.request);
    REQUIRE(report.status.code == ErrorCode::BadFile);
    REQUIRE_FALSE(fs::exists(f.exportDir()));
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE_FALSE(fs::exists(f.blenderFolder / "fake_jobs.log"));
}

TEST_CASE("a single project job and files are as in milestone 10", "[core][export][run]") {
    ExportFixture f("run-single-job");
    plynth::test::requireOk(runExport(f.request).status);
    const Json job = lastJob(f);
    REQUIRE(jobSchemaProblem(job).empty());
    const Json& groups = job["params"]["groups"];
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0]["id"] == "building");
    REQUIRE(groups[0]["file"] == "Square.fbx");
    REQUIRE(groups[0]["name"] == "Square");
    REQUIRE(groups[0]["members"].empty());
    REQUIRE(groups[0]["rest"] == false);
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Textures", "export_report.json"});
}

TEST_CASE("a milestone 10 report still reads", "[core][export][run]") {
    ExportFixture f("run-old-report");
    plynth::test::requireOk(runExport(f.request).status);
    nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    written.erase("groups");
    written.erase("kit");
    written.erase("profile");
    written["outputs"]["files"][0].erase("name");
    std::ofstream(f.reportFile(), std::ios::binary | std::ios::trunc) << written.dump(2);
    REQUIRE(reportProblem(f.reportFile()).empty());
    const ExportReportRead read = readExportReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.fileOutputs.size() == 1);
    REQUIRE(read.report.fileOutputs[0].name.empty());
    REQUIRE(read.report.emptyGroups.empty());
    REQUIRE_FALSE(read.report.kit.asked);
    // Every export before profiles was made for Unreal.
    REQUIRE(read.report.profile == ExportProfile::Unreal);

    written["profile"] = "other";
    std::ofstream(f.reportFile(), std::ios::binary | std::ios::trunc) << written.dump(2);
    const ExportReportRead bad = readExportReport(f.projectFolder);
    REQUIRE(bad.status.code == ErrorCode::BadFile);
    REQUIRE(bad.status.where == "/profile");
}

namespace {

// Makes the request an Unreal export that asks for the kit or not, as the Unreal link does.
void askKit(ExportFixture& f, bool on)
{
    f.request.settings.profile = ExportProfile::Unreal;
    f.request.kit = on;
}

}

TEST_CASE("the kit is written with its layout when the options switch it on", "[core][export][run][kit]") {
    TestLibrary lib("export-kit");
    ExportFixture f("run-kit");
    f.request.library = lib.paths;
    askKit(f, true);

    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Square_Kit.fbx", "Square_Kit_layout.json", "Textures", "export_report.json"});

    const Json job = lastJob(f);
    INFO(jobSchemaProblem(job));
    REQUIRE(jobSchemaProblem(job).empty());
    REQUIRE(job["params"]["profile"] == "unreal");
    REQUIRE(report.profile == ExportProfile::Unreal);
    const Json& kitJob = job["params"]["kit"];
    REQUIRE(kitJob.is_object());
    REQUIRE(kitJob["file"] == "Square_Kit.fbx");
    REQUIRE(kitJob["fill_openings"] == true);
    REQUIRE(kitJob["pieces"].size() >= 2);

    const nlohmann::json layout = nlohmann::json::parse(plynth::test::readTextFile(f.exportDir() / "Square_Kit_layout.json"));
    INFO(plynth::test::schemaProblem("kit-layout.schema.json", layout));
    REQUIRE(plynth::test::schemaProblem("kit-layout.schema.json", layout).empty());
    REQUIRE(layout["kit_file"] == "Square_Kit.fbx");
    REQUIRE(layout["pieces"].size() == kitJob["pieces"].size());
    for (std::size_t i = 0; i < kitJob["pieces"].size(); ++i) REQUIRE(layout["pieces"][i]["id"] == kitJob["pieces"][i]["id"]);

    REQUIRE(report.kit.asked);
    REQUIRE(report.kit.written);
    REQUIRE(report.kit.skipped.empty());
    REQUIRE(report.kit.file == f.exportDir() / "Square_Kit.fbx");
    REQUIRE(report.kit.layoutFile == f.exportDir() / "Square_Kit_layout.json");
    REQUIRE(report.kit.pieces == static_cast<int>(layout["pieces"].size()));
    REQUIRE(report.kit.placements == static_cast<int>(layout["placements"].size()));
    REQUIRE(report.kit.triangles == 100 * report.kit.pieces);
    REQUIRE(reportProblem(f.reportFile()).empty());
    const ExportReportRead read = readExportReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(exportReportToJson(read.report) == exportReportToJson(report));
}

TEST_CASE("without a library the export goes on and says why the kit was left out", "[core][export][run][kit]") {
    ExportFixture f("run-kit-nolib");
    askKit(f, true);
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.exported);
    REQUIRE(report.kit.asked);
    REQUIRE_FALSE(report.kit.written);
    REQUIRE(report.kit.skipped == "No library is set, so the kit was left out.");
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Textures", "export_report.json"});
    REQUIRE(lastJob(f)["params"]["kit"].is_null());
    REQUIRE(reportProblem(f.reportFile()).empty());
}

TEST_CASE("switching the kit off removes the old kit files at the next export", "[core][export][run][kit]") {
    TestLibrary lib("export-kit-off");
    ExportFixture f("run-kit-off");
    f.request.library = lib.paths;
    askKit(f, true);
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(fs::exists(f.exportDir() / "Square_Kit.fbx"));

    askKit(f, false);
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE_FALSE(report.kit.asked);
    REQUIRE_FALSE(report.kit.written);
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Textures", "export_report.json"});
}

TEST_CASE("a blocked export leaves the old kit files", "[core][export][run][kit]") {
    TestLibrary lib("export-kit-blocked");
    ExportFixture f("run-kit-blocked");
    f.request.library = lib.paths;
    askKit(f, true);
    plynth::test::requireOk(runExport(f.request).status);

    f.setMode("quality_fail");
    const ExportReport blocked = runExport(f.request);
    REQUIRE(blocked.status.code == ErrorCode::QualityFailed);
    REQUIRE(fs::exists(f.exportDir() / "Square_Kit.fbx"));
    REQUIRE(fs::exists(f.exportDir() / "Square_Kit_layout.json"));
    REQUIRE(blocked.kit.written);
    REQUIRE(blocked.kit.file == f.exportDir() / "Square_Kit.fbx");

    f.setMode("");
    askKit(f, false);
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE_FALSE(fs::exists(f.exportDir() / "Square_Kit.fbx"));
    REQUIRE_FALSE(fs::exists(f.exportDir() / "Square_Kit_layout.json"));
}

TEST_CASE("a grouped project gets the kit too", "[core][export][run][kit]") {
    TestLibrary lib("export-kit-grouped");
    ExportFixture f("run-kit-grouped");
    makeGrouped(f);
    f.request.library = lib.paths;
    askKit(f, true);
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.kit.written);
    REQUIRE(fs::exists(f.exportDir() / "Square_Kit.fbx"));
    REQUIRE(fs::exists(f.exportDir() / "Square_Walls.fbx"));
    REQUIRE(jobSchemaProblem(lastJob(f)).empty());
}

TEST_CASE("a check only run makes no kit", "[core][export][run][kit]") {
    TestLibrary lib("export-kit-check");
    ExportFixture f("run-kit-check");
    f.request.library = lib.paths;
    f.request.settings.write = false;
    askKit(f, true);
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(lastJob(f)["params"]["kit"].is_null());
    REQUIRE_FALSE(fs::exists(f.exportDir() / "Square_Kit.fbx"));
}

TEST_CASE("the job gives each library material its tile size", "[core][export][run]") {
    MaterialTestLibrary lib("export-tile");
    lib.add("oak_planks", "Oak Planks");
    ExportFixture f("run-tile");
    f.request.library = lib.paths;
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "export" / "materials.json", f.projectFolder / "build" / "materials.json");
    plynth::test::requireOk(runExport(f.request).status);

    const Json job = lastJob(f);
    INFO(jobSchemaProblem(job));
    REQUIRE(jobSchemaProblem(job).empty());
    const Json& materials = job["params"]["materials"];
    REQUIRE(materials.size() == 3);
    REQUIRE(materials[0]["material"] == "oak_planks");
    REQUIRE(materials[0]["tile_size"] == Json{{"width", 1.0}, {"height", 0.5}});
    REQUIRE_FALSE(materials[1].contains("tile_size"));
    REQUIRE_FALSE(materials[2].contains("tile_size"));

    // Without a library there is nothing to look up.
    f.request.library.reset();
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE_FALSE(lastJob(f)["params"]["materials"][0].contains("tile_size"));
}

TEST_CASE("kit cut faces have no wall in the job", "[core][export][run][kit]") {
    TestLibrary lib("export-kit-null-wall");
    ExportFixture f("run-kit-null-wall");
    f.request.library = lib.paths;
    askKit(f, true);
    plynth::test::requireOk(runExport(f.request).status);
    const Json job = lastJob(f);
    REQUIRE(jobSchemaProblem(job).empty());
    int cuts = 0;
    for (const Json& piece : job["params"]["kit"]["pieces"]) {
        for (const Json& solid : piece["wall_solids"]) {
            for (const Json& edge : solid["outline"]["edges"]) {
                if (edge.contains("kit_cut")) {
                    REQUIRE(edge["wall"].is_null());
                    ++cuts;
                }
            }
        }
    }
    REQUIRE(cuts > 0);
}

TEST_CASE("a group named Kit does not take the kit file name", "[core][export][run][kit]") {
    TestLibrary lib("export-kit-name");
    ExportFixture f("run-kit-name");
    makeGrouped(f);
    f.request.library = lib.paths;
    f.request.settings.force = true;
    askKit(f, true);
    ExportOptions options;
    options.groups.renames = {{"furniture_r1", "Kit"}};
    plynth::test::requireOk(writeExportOptions(f.projectFolder, options));
    plynth::test::requireOk(runExport(f.request).status);
    // The second export, forced, must not lose either file.
    const ExportReport second = runExport(f.request);
    plynth::test::requireOk(second.status);
    REQUIRE(plynth::test::readTextFile(f.exportDir() / "Square_Kit.fbx") == "FAKEKIT");
    REQUIRE(plynth::test::readTextFile(f.exportDir() / "Square_Kit_2.fbx") == "FAKEFBX");
    REQUIRE(fs::exists(f.exportDir() / "Square_Kit_layout.json"));
}

TEST_CASE("the built tile size wins over the library's", "[core][export][run]") {
    MaterialTestLibrary lib("export-built-tile");
    lib.add("oak_planks", "Oak Planks");
    ExportFixture f("run-built-tile");
    f.request.library = lib.paths;
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "export" / "materials_tile.json", f.projectFolder / "build" / "materials.json");
    plynth::test::requireOk(runExport(f.request).status);
    const Json job = lastJob(f);
    REQUIRE(jobSchemaProblem(job).empty());
    REQUIRE(job["params"]["materials"][0]["tile_size"] == Json{{"width", 2.0}, {"height", 3.0}});

    // With the field and no library the job still has it.
    f.request.library.reset();
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(lastJob(f)["params"]["materials"][0]["tile_size"] == Json{{"width", 2.0}, {"height", 3.0}});
}

TEST_CASE("an entry without a built tile size gets the library's", "[core][export][run]") {
    MaterialTestLibrary lib("export-no-built-tile");
    lib.add("oak_planks", "Oak Planks");
    ExportFixture f("run-no-built-tile");
    f.request.library = lib.paths;
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "export" / "materials.json", f.projectFolder / "build" / "materials.json");
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(lastJob(f)["params"]["materials"][0]["tile_size"] == Json{{"width", 1.0}, {"height", 0.5}});
}

TEST_CASE("an opening the plan cannot build leaves the kit out with a reason", "[core][export][run][kit]") {
    TestLibrary lib("export-kit-bad-opening");
    ExportFixture f("run-kit-bad-opening");
    f.request.library = lib.paths;
    Opening window;
    window.id = "o1";
    window.wall = "w1";
    window.offset = 2.0;
    window.type = OpeningType::Window;
    window.width = 1.0;
    window.height = 1.0;
    window.sill = 0.9;
    window.shape = "shape_that_nobody_registered";
    f.request.project.plan.floors[0].openings.push_back(window);
    askKit(f, true);

    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.kit.asked);
    REQUIRE_FALSE(report.kit.written);
    REQUIRE_FALSE(report.kit.skipped.empty());
    REQUIRE(report.kit.skipped.find("o1") != std::string::npos);
    REQUIRE(report.kit.skipped.find("cannot be built") != std::string::npos);
    REQUIRE(lastJob(f)["params"]["kit"].is_null());
    REQUIRE(f.exportEntries().count("Square_Kit.fbx") == 0);
    REQUIRE(f.exportEntries().count("Square_Kit_layout.json") == 0);
    REQUIRE(reportProblem(f.reportFile()).empty());
}

TEST_CASE("a plain export never makes the kit or the gameplay data, even when asked", "[core][export][run][kit][profile]") {
    TestLibrary lib("export-plain-kit");
    ExportFixture f("run-plain-kit");
    f.request.library = lib.paths;
    f.request.kit = true;
    f.request.gameplay = true;
    ExportOptions options;
    options.links[std::string(kUnrealLinkId)].kit = true;
    options.links[std::string(kUnrealLinkId)].gameplay = true;
    plynth::test::requireOk(writeExportOptions(f.projectFolder, options));

    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.profile == ExportProfile::Plain);
    REQUIRE_FALSE(report.kit.asked);
    REQUIRE_FALSE(report.kit.written);
    REQUIRE_FALSE(report.gameplay.enabled);
    const Json job = lastJob(f);
    INFO(jobSchemaProblem(job));
    REQUIRE(jobSchemaProblem(job).empty());
    REQUIRE(job["params"]["profile"] == "plain");
    REQUIRE(job["params"]["kit"].is_null());
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Textures", "export_report.json"});
    REQUIRE(reportProblem(f.reportFile()).empty());
    const ExportReportRead read = readExportReport(f.projectFolder);
    plynth::test::requireOk(read.status);
    REQUIRE(read.report.profile == ExportProfile::Plain);
    REQUIRE(exportReportToJson(read.report) == exportReportToJson(report));
}

TEST_CASE("an Unreal export writes the kit and gameplay data it is asked for", "[core][export][run][kit][profile]") {
    TestLibrary lib("export-unreal-asked");
    ExportFixture f("run-unreal-asked");
    f.request.library = lib.paths;
    f.request.settings.profile = ExportProfile::Unreal;
    f.request.kit = true;
    f.request.gameplay = true;
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.kit.written);
    REQUIRE(report.gameplay.enabled);
    REQUIRE(lastJob(f)["params"]["kit"].is_object());
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Square_Kit.fbx", "Square_Kit_layout.json", "Square_gameplay.json",
                                                       "Textures", "export_report.json"});
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(f.reportFile()));
    REQUIRE(written["profile"] == "unreal");
    REQUIRE(reportProblem(f.reportFile()).empty());

    // The same request with the plain profile removes them again.
    f.request.settings.profile = ExportProfile::Plain;
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(f.exportEntries() == std::set<std::string>{"Square.fbx", "Textures", "export_report.json"});
}

TEST_CASE("an export into its own output folder leaves export/ alone", "[core][export][run][profile]") {
    ExportFixture f("run-output-folder");
    const fs::path out = f.projectFolder / "build" / "links" / "unreal" / "out";
    f.request.outputFolder = out;
    f.request.settings.profile = ExportProfile::Unreal;
    const ExportReport report = runExport(f.request);
    plynth::test::requireOk(report.status);
    REQUIRE(report.files == std::vector<fs::path>{out / "Square.fbx"});
    REQUIRE(report.reportFile == out / "export_report.json");
    REQUIRE(report.texturesFolder == out / "Textures");
    REQUIRE(fs::is_regular_file(out / "Square.fbx"));
    REQUIRE(fs::is_regular_file(out / "Textures" / "T_Fake_BaseColor.png"));
    REQUIRE(fs::is_regular_file(out / "export_report.json"));
    REQUIRE_FALSE(fs::exists(out / ".staging"));
    REQUIRE_FALSE(fs::exists(f.exportDir()));
    REQUIRE(f.lastJobParams()["output_folder"] == pathToUtf8(out / ".staging"));
    REQUIRE(readExportReport(f.projectFolder).status.code == ErrorCode::NotFound);
    REQUIRE(reportProblem(out / "export_report.json").empty());

    // The next export there removes the files the report in that folder lists.
    f.request.project.settings.name = "Other";
    plynth::test::requireOk(runExport(f.request).status);
    REQUIRE(fs::is_regular_file(out / "Other.fbx"));
    REQUIRE_FALSE(fs::exists(out / "Square.fbx"));
    REQUIRE_FALSE(fs::exists(f.exportDir()));

    // A blocked export there keeps the file list of the last good one.
    f.setMode("quality_fail");
    REQUIRE(runExport(f.request).status.code == ErrorCode::QualityFailed);
    REQUIRE(fs::is_regular_file(out / "Other.fbx"));
    const nlohmann::json written = nlohmann::json::parse(plynth::test::readTextFile(out / "export_report.json"));
    REQUIRE(written["outputs"]["files"][0]["file"] == "Other.fbx");
    REQUIRE_FALSE(fs::exists(f.exportDir()));
}
