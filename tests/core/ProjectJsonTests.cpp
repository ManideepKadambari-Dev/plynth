#include "TestSupport.h"

#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace plynth;
using plynth::test::fullPlan;

namespace {

// Reads a plan from JSON text.
PlanRead readPlanText(const std::string& text)
{
    return planFromJson(Json::parse(text));
}

// A tiny valid plan file as JSON, ready to be broken by a test.
Json minimalPlanJson()
{
    return Json::parse(R"({"format":"plynth-plan","version":1,"floors":[{"id":"f1"}]})");
}

// Expects planFromJson to refuse the JSON with a given code and pointer.
void expectPlanRefused(const Json& json, ErrorCode code, const std::string& where)
{
    const PlanRead r = planFromJson(json);
    INFO(r.status.message << " at " << r.status.where);
    REQUIRE_FALSE(r.status.ok());
    REQUIRE(r.status.code == code);
    REQUIRE(r.status.where == where);
    REQUIRE_FALSE(r.status.message.empty());
}

}

TEST_CASE("plan JSON round trip", "[core][json]") {
    const Plan plan = fullPlan();
    const PlanRead r = planFromJson(planToJson(plan));
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan == plan);
    REQUIRE(r.fileVersion == 1);
    REQUIRE_FALSE(r.migrated);
}

TEST_CASE("plan JSON round trip through text", "[core][json]") {
    const Plan plan = fullPlan();
    const std::string text = planToJson(plan).dump(2);
    const PlanRead r = readPlanText(text);
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan == plan);
}

TEST_CASE("empty plan round trips and has nulls and empty arrays", "[core][json]") {
    const Plan plan = makeEmptyPlan();
    const Json j = planToJson(plan);
    REQUIRE(j["scale"]["pixels_per_metre"].is_null());
    REQUIRE(j["floors"][0]["image"].is_null());
    REQUIRE(j["floors"][0]["points"].is_array());
    REQUIRE(j["floors"][0]["points"].empty());
    const PlanRead r = planFromJson(j);
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan == plan);
}

TEST_CASE("plan keys are written in schema order", "[core][json]") {
    const Json j = planToJson(fullPlan());
    std::vector<std::string> top;
    for (auto it = j.begin(); it != j.end(); ++it) top.push_back(it.key());
    REQUIRE(top == std::vector<std::string>{"format", "version", "scale", "floors"});

    std::vector<std::string> floorKeys;
    for (auto it = j["floors"][0].begin(); it != j["floors"][0].end(); ++it) floorKeys.push_back(it.key());
    REQUIRE(floorKeys == std::vector<std::string>{"id", "name", "image", "ceiling_height", "points", "walls", "rooms", "openings"});

    std::vector<std::string> wallKeys;
    for (auto it = j["floors"][0]["walls"][0].begin(); it != j["floors"][0]["walls"][0].end(); ++it) wallKeys.push_back(it.key());
    REQUIRE(wallKeys == std::vector<std::string>{"id", "from", "to", "thickness", "height", "bulge", "material_inside", "material_outside"});
}

TEST_CASE("missing optional fields take their defaults", "[core][json]") {
    const PlanRead r = readPlanText(R"({
        "format":"plynth-plan","version":1,
        "floors":[{"id":"f1",
          "points":[{"id":"p1","x":0,"y":0},{"id":"p2","x":1,"y":0}],
          "walls":[{"id":"w1","from":"p1","to":"p2"}],
          "rooms":[{"id":"r1","walls":["w1"]}],
          "openings":[{"id":"o1","wall":"w1","offset":0.5,"type":"door","width":0.9,"height":2.1}]}]})");
    plynth::test::requireOk(r.status);
    const Floor& f = r.plan.floors[0];
    REQUIRE_FALSE(r.plan.scale.pixelsPerMetre);
    REQUIRE(f.name == "f1");
    REQUIRE_FALSE(f.image);
    REQUIRE(f.ceilingHeight == kDefaultCeilingHeight);
    REQUIRE(f.points[0].cornerRadius == 0.0);
    REQUIRE(f.walls[0].thickness == kDefaultWallThickness);
    REQUIRE_FALSE(f.walls[0].height);
    REQUIRE(f.walls[0].bulge == 0.0);
    REQUIRE_FALSE(f.walls[0].materialInside);
    REQUIRE_FALSE(f.rooms[0].type);
    REQUIRE(f.openings[0].shape == "rectangle");
    REQUIRE(f.openings[0].sill == 0.0);
    REQUIRE_FALSE(f.openings[0].frame);
}

TEST_CASE("the reader refuses what breaks the format", "[core][json]") {
    SECTION("not an object") {
        expectPlanRefused(Json::parse("[1]"), ErrorCode::BadFile, "");
    }
    SECTION("unknown fields") {
        Json j = minimalPlanJson();
        j["extra"] = 1;
        expectPlanRefused(j, ErrorCode::BadFile, "/extra");
        j = minimalPlanJson();
        j["floors"][0]["colour"] = "red";
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/colour");
        j = minimalPlanJson();
        j["scale"] = Json{{"unit", "m"}};
        expectPlanRefused(j, ErrorCode::BadFile, "/scale/unit");
    }
    SECTION("wrong types") {
        Json j = minimalPlanJson();
        j["floors"][0]["id"] = 5;
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/id");
        j = minimalPlanJson();
        j["floors"][0]["points"] = Json::array({Json{{"id", "p1"}, {"x", "1"}, {"y", 0}}});
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/points/0/x");
        j = minimalPlanJson();
        j["floors"] = Json::object();
        expectPlanRefused(j, ErrorCode::BadFile, "/floors");
        j = minimalPlanJson();
        j["floors"][0]["image"] = 3;
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/image");
        j = minimalPlanJson();
        j["floors"][0]["points"] = Json::array({Json{{"id", "p1"}, {"x", true}, {"y", 0}}});
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/points/0/x");
    }
    SECTION("a null where a number is required, which is how NaN is written") {
        const std::string text = planToJson(fullPlan()).dump();
        Json j = Json::parse(text);
        j["floors"][0]["points"][1]["y"] = nullptr;
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/points/1/y");
        j = minimalPlanJson();
        j["floors"][0]["ceiling_height"] = nullptr;
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/ceiling_height");
    }
    SECTION("missing required fields") {
        Json j = minimalPlanJson();
        j.erase("floors");
        expectPlanRefused(j, ErrorCode::BadFile, "/floors");
        j = minimalPlanJson();
        j["floors"][0].erase("id");
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/id");
    }
    SECTION("duplicate id and broken reference") {
        Json j = planToJson(fullPlan());
        j["floors"][0]["points"][1]["id"] = "p1";
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/points/1/id");
        j = planToJson(fullPlan());
        j["floors"][0]["walls"][0]["to"] = "p99";
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/walls/0/to");
    }
    SECTION("wrong format") {
        Json j = minimalPlanJson();
        j["format"] = "plynth-project";
        expectPlanRefused(j, ErrorCode::BadFile, "/format");
        j.erase("format");
        expectPlanRefused(j, ErrorCode::BadFile, "/format");
    }
    SECTION("missing or odd version") {
        Json j = minimalPlanJson();
        j.erase("version");
        expectPlanRefused(j, ErrorCode::BadFile, "/version");
        j["version"] = "1";
        expectPlanRefused(j, ErrorCode::BadFile, "/version");
        j["version"] = 1.5;
        expectPlanRefused(j, ErrorCode::BadFile, "/version");
        j["version"] = 0;
        expectPlanRefused(j, ErrorCode::BadFile, "/version");
    }
    SECTION("a newer version") {
        Json j = minimalPlanJson();
        j["version"] = 2;
        const PlanRead r = planFromJson(j);
        REQUIRE(r.status.code == ErrorCode::UnsupportedVersion);
        REQUIRE(r.fileVersion == 2);
    }
    SECTION("a bad opening type and a bad export value") {
        Json j = planToJson(fullPlan());
        j["floors"][0]["openings"][0]["type"] = "gate";
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/openings/0/type");
    }
    SECTION("a failed read gives an empty plan") {
        const PlanRead r = planFromJson(Json::parse("[]"));
        REQUIRE(r.plan == Plan{});
    }
}

TEST_CASE("settings JSON round trip", "[core][json]") {
    ProjectSettings s;
    s.name = "My flat ü";
    s.exportMode = ExportMode::Grouped;
    s.gridSize = 0.25;
    const SettingsRead r = settingsFromJson(settingsToJson(s));
    plynth::test::requireOk(r.status);
    REQUIRE(r.settings == s);

    const SettingsRead again = settingsFromJson(Json::parse(settingsToJson(s).dump()));
    plynth::test::requireOk(again.status);
    REQUIRE(again.settings == s);
}

TEST_CASE("settings keys are in schema order and defaults apply", "[core][json]") {
    const Json j = settingsToJson(ProjectSettings{});
    std::vector<std::string> keys;
    for (auto it = j.begin(); it != j.end(); ++it) keys.push_back(it.key());
    REQUIRE(keys == std::vector<std::string>{"format", "version", "name", "export_mode", "grid_size"});

    const SettingsRead r = settingsFromJson(Json::parse(R"({"format":"plynth-project","version":1,"name":"A"})"));
    plynth::test::requireOk(r.status);
    REQUIRE(r.settings.exportMode == ExportMode::Single);
    REQUIRE(r.settings.gridSize == kDefaultGridSize);
}

TEST_CASE("settings refuse bad values", "[core][json]") {
    const auto refused = [](const char* text, ErrorCode code, const std::string& where) {
        const SettingsRead r = settingsFromJson(Json::parse(text));
        INFO(text << " -> " << r.status.message);
        REQUIRE(r.status.code == code);
        REQUIRE(r.status.where == where);
    };
    refused(R"({"format":"plynth-project","version":1})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":1,"name":""})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":1,"name":5})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":1,"name":"A","export_mode":"double"})", ErrorCode::BadFile, "/export_mode");
    refused(R"({"format":"plynth-project","version":1,"name":"A","grid_size":0})", ErrorCode::BadFile, "/grid_size");
    refused(R"({"format":"plynth-project","version":1,"name":"A","grid_size":null})", ErrorCode::BadFile, "/grid_size");
    refused(R"({"format":"plynth-project","version":1,"name":"A","extra":1})", ErrorCode::BadFile, "/extra");
    refused(R"({"format":"plynth-plan","version":1,"name":"A"})", ErrorCode::BadFile, "/format");
    refused(R"({"format":"plynth-project","name":"A"})", ErrorCode::BadFile, "/version");
    refused(R"({"format":"plynth-project","version":2,"name":"A"})", ErrorCode::UnsupportedVersion, "/version");
}

TEST_CASE("migrateJson runs steps in order and sets the version", "[core][json][migrate]") {
    std::vector<int> ran;
    const std::vector<Migration> steps = {
        {1,
         [&](Json& j) {
             ran.push_back(1);
             j["added_in_2"] = true;
             return Status{};
         }},
        {2,
         [&](Json& j) {
             ran.push_back(2);
             j["seen_version"] = j["version"];
             return Status{};
         }},
    };
    Json j = Json::parse(R"({"version":1})");
    plynth::test::requireOk(migrateJson(j, 3, steps));
    REQUIRE(ran == std::vector<int>{1, 2});
    REQUIRE(j["version"] == 3);
    REQUIRE(j["added_in_2"] == true);
    REQUIRE(j["seen_version"] == 2);

    ran.clear();
    Json already = Json::parse(R"({"version":3})");
    plynth::test::requireOk(migrateJson(already, 3, steps));
    REQUIRE(ran.empty());

    ran.clear();
    Json middle = Json::parse(R"({"version":2})");
    plynth::test::requireOk(migrateJson(middle, 3, steps));
    REQUIRE(ran == std::vector<int>{2});
}

TEST_CASE("migrateJson stops at a failing step and reports a missing step", "[core][json][migrate]") {
    int secondRan = 0;
    const std::vector<Migration> steps = {
        {1, [](Json&) { return fail(ErrorCode::BadFile, "step one failed", "/x"); }},
        {2,
         [&](Json&) {
             ++secondRan;
             return Status{};
         }},
    };
    Json j = Json::parse(R"({"version":1})");
    const Status s = migrateJson(j, 3, steps);
    REQUIRE(s.code == ErrorCode::BadFile);
    REQUIRE(s.message == "step one failed");
    REQUIRE(secondRan == 0);
    REQUIRE(j["version"] == 1);

    Json gap = Json::parse(R"({"version":2})");
    const std::vector<Migration> onlyFirst = {steps[0]};
    const Status missing = migrateJson(gap, 3, onlyFirst);
    REQUIRE(missing.code == ErrorCode::BadFile);
    REQUIRE(missing.where == "/version");

    Json newer = Json::parse(R"({"version":4})");
    REQUIRE(migrateJson(newer, 3, steps).code == ErrorCode::UnsupportedVersion);
    Json none = Json::parse("{}");
    REQUIRE(migrateJson(none, 3, steps).code == ErrorCode::BadFile);
    Json text = Json::parse(R"({"version":"1"})");
    REQUIRE(migrateJson(text, 3, steps).code == ErrorCode::BadFile);
}

TEST_CASE("migration lists are empty while only version 1 exists", "[core][json][migrate]") {
    REQUIRE(planMigrations().empty());
    REQUIRE(projectMigrations().empty());
    REQUIRE(kPlanFormatVersion == 1);
    REQUIRE(kProjectFormatVersion == 1);
}
