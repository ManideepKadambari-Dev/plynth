#include "SchemaSupport.h"
#include "TestSupport.h"

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
    for (const char* file : {"plan.schema.json", "project.schema.json"}) {
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
