#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/Export.h>
#include <plynth/core/Exporter.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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

TEST_CASE("the single exporter is registered and the grouped one is not", "[core][export]") {
    const Exporter* single = findExporter("single");
    REQUIRE(single);
    REQUIRE(single->id() == kSingleExporterId);
    REQUIRE(single->name() == "Single FBX");
    REQUIRE_FALSE(single->description().empty());
    REQUIRE_FALSE(findExporter("grouped"));

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
    REQUIRE(workerSteps == 7);

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
    REQUIRE(report.steps.size() == 8);
    REQUIRE(report.steps[0].step == "prepare");
    REQUIRE(report.steps[1].step == "open_blend");
    REQUIRE(report.steps[7].step == "file_checks");

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
    REQUIRE(report.files.empty());
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
    REQUIRE(written["outputs"]["files"].empty());
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

TEST_CASE("the job carries the materials of the build", "[core][export][run]") {
    ExportFixture f("run-materials");
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "export" / "materials.json", f.projectFolder / "build" / "materials.json");
    plynth::test::requireOk(runExport(f.request).status);
    const Json materials = f.lastJobParams()["materials"];
    REQUIRE(materials.size() == 3);
    REQUIRE(materials[0]["name"] == "Living_Floor");
}

TEST_CASE("a project in grouped mode is refused and nothing is written", "[core][export][run]") {
    ExportFixture f("run-grouped");
    f.request.project.settings.exportMode = ExportMode::Grouped;
    const ExportReport report = runExport(f.request);
    REQUIRE(report.status.code == ErrorCode::InvalidValue);
    REQUIRE(report.status.message.find("not available yet") != std::string::npos);
    REQUIRE_FALSE(fs::exists(f.exportDir()));
    REQUIRE_FALSE(fs::exists(f.lockFile()));
    REQUIRE_FALSE(fs::exists(f.blenderFolder / "fake_jobs.log"));
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
