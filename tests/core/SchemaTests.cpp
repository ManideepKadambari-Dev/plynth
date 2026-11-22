#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Export.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
using plynth::test::fullPlan;
using plynth::test::schemaProblem;

namespace {

// Lists the data files whose names start with a prefix, sorted so the order is stable.
std::vector<std::filesystem::path> dataFiles(const std::string& prefix)
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(PLYNTH_TEST_DATA_DIR)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Turns the ordered JSON that core writes into the plain JSON the validator takes.
nlohmann::json plain(const Json& j)
{
    return nlohmann::json::parse(j.dump());
}

}

TEST_CASE("the schema files are valid schemas", "[core][schema]") {
    for (const char* file : {"plan.schema.json", "project.schema.json", "plan.v1.schema.json", "project.v1.schema.json", "plan.v2.schema.json",
                             "project.v2.schema.json", "plan.v3.schema.json", "project.v3.schema.json", "settings.schema.json", "settings.v1.schema.json",
                             "settings.v2.schema.json", "export-report.schema.json"}) {
        INFO(file);
        // An empty object is not a plan or a project, so a working validator complains about it
        // with a message about the instance, not about reading or loading the schema.
        const std::string problem = schemaProblem(file, nlohmann::json::object());
        REQUIRE_FALSE(problem.empty());
        REQUIRE(problem.find("could not read") == std::string::npos);
    }
}

TEST_CASE("written plans validate against the plan schema", "[core][schema]") {
    REQUIRE(schemaProblem("plan.schema.json", plain(planToJson(fullPlan()))).empty());
    REQUIRE(schemaProblem("plan.schema.json", plain(planToJson(makeEmptyPlan()))).empty());
    REQUIRE(schemaProblem("plan.schema.json", plain(planToJson(plynth::test::smallPlan()))).empty());
}

TEST_CASE("written settings validate against the project schema", "[core][schema]") {
    ProjectSettings s;
    REQUIRE(schemaProblem("project.schema.json", plain(settingsToJson(s))).empty());
    s.name = "Flat ü";
    s.exportMode = ExportMode::Grouped;
    s.gridSize = 0.1;
    REQUIRE(schemaProblem("project.schema.json", plain(settingsToJson(s))).empty());
}

TEST_CASE("valid plan files pass the schema and the reader", "[core][schema]") {
    const auto files = dataFiles("valid-");
    REQUIRE(files.size() >= 3);
    for (const auto& file : files) {
        INFO(file.filename().string());
        const std::string text = plynth::test::readTextFile(file);
        REQUIRE(schemaProblem("plan.schema.json", nlohmann::json::parse(text)).empty());
        const PlanRead r = planFromJson(Json::parse(text));
        plynth::test::requireOk(r.status);
    }
}

TEST_CASE("invalid plan files fail the schema and the reader", "[core][schema]") {
    const auto files = dataFiles("invalid-");
    REQUIRE(files.size() >= 8);
    for (const auto& file : files) {
        INFO(file.filename().string());
        const std::string text = plynth::test::readTextFile(file);
        REQUIRE_FALSE(schemaProblem("plan.schema.json", nlohmann::json::parse(text)).empty());
        REQUIRE_FALSE(planFromJson(Json::parse(text)).status.ok());
    }
}

TEST_CASE("rules a schema cannot express are caught by the reader alone", "[core][schema]") {
    const auto files = dataFiles("semantic-");
    REQUIRE(files.size() >= 3);
    for (const auto& file : files) {
        INFO(file.filename().string());
        const std::string text = plynth::test::readTextFile(file);
        REQUIRE(schemaProblem("plan.schema.json", nlohmann::json::parse(text)).empty());
        REQUIRE(planFromJson(Json::parse(text)).status.code == ErrorCode::BadFile);
    }
}

TEST_CASE("valid project files pass the schema and the reader", "[core][schema]") {
    const auto files = dataFiles("project-valid-");
    REQUIRE(files.size() >= 2);
    for (const auto& file : files) {
        INFO(file.filename().string());
        const std::string text = plynth::test::readTextFile(file);
        REQUIRE(schemaProblem("project.schema.json", nlohmann::json::parse(text)).empty());
        plynth::test::requireOk(settingsFromJson(Json::parse(text)).status);
    }
}

TEST_CASE("invalid project files fail the schema and the reader", "[core][schema]") {
    const auto files = dataFiles("project-invalid-");
    REQUIRE(files.size() >= 5);
    for (const auto& file : files) {
        INFO(file.filename().string());
        const std::string text = plynth::test::readTextFile(file);
        REQUIRE_FALSE(schemaProblem("project.schema.json", nlohmann::json::parse(text)).empty());
        REQUIRE_FALSE(settingsFromJson(Json::parse(text)).status.ok());
    }
}

TEST_CASE("version 1 files pass the version 1 schemas and upgrade", "[core][schema][migrate]") {
    const auto dir = std::filesystem::path(PLYNTH_TEST_DATA_DIR);
    const nlohmann::json plan = nlohmann::json::parse(plynth::test::readTextFile(dir / "v1-full.json"));
    REQUIRE(schemaProblem("plan.v1.schema.json", plan).empty());
    REQUIRE_FALSE(schemaProblem("plan.schema.json", plan).empty());
    const PlanRead r = planFromJson(Json::parse(plan.dump()));
    plynth::test::requireOk(r.status);
    REQUIRE(r.migrated);
    const nlohmann::json upgraded = plain(planToJson(r.plan));
    REQUIRE(upgraded["version"] == 4);
    REQUIRE(schemaProblem("plan.schema.json", upgraded).empty());

    const nlohmann::json project = nlohmann::json::parse(plynth::test::readTextFile(dir / "v1-project.json"));
    REQUIRE(schemaProblem("project.v1.schema.json", project).empty());
    REQUIRE_FALSE(schemaProblem("project.schema.json", project).empty());
    const SettingsRead s = settingsFromJson(Json::parse(project.dump()));
    plynth::test::requireOk(s.status);
    REQUIRE(schemaProblem("project.schema.json", plain(settingsToJson(s.settings))).empty());
}

TEST_CASE("version 2 files pass the version 2 schemas and upgrade", "[core][schema][migrate]") {
    const auto dir = std::filesystem::path(PLYNTH_TEST_DATA_DIR);
    for (const char* name : {"v2-full.json", "v2-minimal.json"}) {
        INFO(name);
        const nlohmann::json plan = nlohmann::json::parse(plynth::test::readTextFile(dir / name));
        REQUIRE(schemaProblem("plan.v2.schema.json", plan).empty());
        REQUIRE_FALSE(schemaProblem("plan.schema.json", plan).empty());
        const PlanRead r = planFromJson(Json::parse(plan.dump()));
        plynth::test::requireOk(r.status);
        REQUIRE(r.migrated);
        REQUIRE(schemaProblem("plan.schema.json", plain(planToJson(r.plan))).empty());
    }

    const nlohmann::json project = nlohmann::json::parse(plynth::test::readTextFile(dir / "v2-project.json"));
    REQUIRE(schemaProblem("project.v2.schema.json", project).empty());
    REQUIRE_FALSE(schemaProblem("project.schema.json", project).empty());
    const SettingsRead s = settingsFromJson(Json::parse(project.dump()));
    plynth::test::requireOk(s.status);
    REQUIRE(s.migrated);
    REQUIRE(schemaProblem("project.schema.json", plain(settingsToJson(s.settings))).empty());
}

TEST_CASE("version 3 files pass the version 3 schemas and upgrade", "[core][schema][migrate]") {
    const auto dir = std::filesystem::path(PLYNTH_TEST_DATA_DIR);
    for (const char* name : {"v3-full.json", "v3-minimal.json"}) {
        INFO(name);
        const nlohmann::json plan = nlohmann::json::parse(plynth::test::readTextFile(dir / name));
        REQUIRE(schemaProblem("plan.v3.schema.json", plan).empty());
        REQUIRE_FALSE(schemaProblem("plan.schema.json", plan).empty());
        const PlanRead r = planFromJson(Json::parse(plan.dump()));
        plynth::test::requireOk(r.status);
        REQUIRE(r.migrated);
        REQUIRE(r.fileVersion == 3);
        for (const Floor& floor : r.plan.floors) {
            for (const Room& room : floor.rooms) REQUIRE_FALSE(room.wallMaterial.has_value());
        }
        const nlohmann::json saved = plain(planToJson(r.plan));
        REQUIRE(saved["version"] == 4);
        REQUIRE(schemaProblem("plan.schema.json", saved).empty());
        REQUIRE_FALSE(schemaProblem("plan.v3.schema.json", saved).empty());
    }

    for (const char* name : {"v3-project.json", "v3-project-minimal.json"}) {
        INFO(name);
        const nlohmann::json project = nlohmann::json::parse(plynth::test::readTextFile(dir / name));
        REQUIRE(schemaProblem("project.v3.schema.json", project).empty());
        REQUIRE_FALSE(schemaProblem("project.schema.json", project).empty());
        const SettingsRead s = settingsFromJson(Json::parse(project.dump()));
        plynth::test::requireOk(s.status);
        REQUIRE(s.migrated);
        REQUIRE(s.fileVersion == 3);
        REQUIRE(s.settings.materials == MaterialDefaults{});
        const nlohmann::json saved = plain(settingsToJson(s.settings));
        REQUIRE(saved["version"] == 4);
        REQUIRE(schemaProblem("project.schema.json", saved).empty());
        REQUIRE_FALSE(schemaProblem("project.v3.schema.json", saved).empty());
    }
}

TEST_CASE("a version 5 plan is unsupported", "[core][schema]") {
    const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "invalid-version-5.json");
    REQUIRE(planFromJson(Json::parse(text)).status.code == ErrorCode::UnsupportedVersion);
}

TEST_CASE("a plan with items and a wall item centre validate", "[core][schema]") {
    ProjectSettings s;
    s.defaults.wallItemCentre = 1.5;
    REQUIRE(schemaProblem("project.schema.json", plain(settingsToJson(s))).empty());
    const nlohmann::json plan = plain(planToJson(fullPlan()));
    REQUIRE(plan["floors"][0]["items"].size() == 2);
    REQUIRE(schemaProblem("plan.schema.json", plan).empty());
}

TEST_CASE("written export reports validate against the export report schema", "[core][schema][export]") {
    ExportReport report;
    report.jobId = "export-3f9a07c1";
    report.startedAt = sessionNow();
    report.exporter = "single";
    report.planFingerprint = "0123456789abcdef";
    report.buildFingerprint = "0123456789abcdef";
    report.exported = true;
    report.forced = true;
    report.overridden = {"naming"};
    report.files = {"D:/Flat/export/Flat.fbx"};
    report.fileOutputs = {{"building", "Flat.fbx", 40, 18200}};
    report.texturesFolder = "D:/Flat/export/Textures";
    report.textureCount = 2;
    report.reportFile = "D:/Flat/export/export_report.json";
    report.logFile = "D:/Flat/build/export_work/blender.log";
    report.objects = 40;
    report.triangles = 18200;
    report.collisionObjects = 31;
    report.blenderVersion = "5.2.0";
    QualityCheckResult naming;
    naming.id = "naming";
    naming.name = "Naming";
    naming.status = QualityStatus::Fail;
    naming.message = "One name breaks the rules.";
    naming.findings = {{"SM_f1_Walls", "w3", "The name has a space."}, {"", "", "A slot name repeats."}};
    QualityCheckResult scale;
    scale.id = "fbx_scale";
    scale.name = "File scale";
    scale.phase = QualityPhase::File;
    scale.status = QualityStatus::Skipped;
    scale.message = "Nothing was written.";
    report.checks = {naming, scale};
    report.steps = {{"prepare", 3}, {"open_blend", 410}};

    REQUIRE(schemaProblem("export-report.schema.json", plain(exportReportToJson(report))).empty());

    ExportReport blocked = report;
    blocked.status = fail(ErrorCode::QualityFailed, "The export was blocked.");
    blocked.exported = false;
    blocked.forced = false;
    blocked.overridden.clear();
    blocked.files.clear();
    blocked.fileOutputs.clear();
    blocked.texturesFolder.clear();
    blocked.textureCount = 0;
    blocked.buildFingerprint.clear();
    REQUIRE(schemaProblem("export-report.schema.json", plain(exportReportToJson(blocked))).empty());

    nlohmann::json bad = plain(exportReportToJson(report));
    bad["quality"]["checks"][0]["status"] = "maybe";
    REQUIRE_FALSE(schemaProblem("export-report.schema.json", bad).empty());
}
