#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
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
    return Json::parse(R"({"format":"plynth-plan","version":2,"floors":[{"id":"f1"}]})");
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
    REQUIRE(r.fileVersion == 2);
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

    std::vector<std::string> openingKeys;
    for (auto it = j["floors"][0]["openings"][0].begin(); it != j["floors"][0]["openings"][0].end(); ++it) openingKeys.push_back(it.key());
    REQUIRE(openingKeys == std::vector<std::string>{"id", "wall", "offset", "type", "shape", "width", "height", "sill", "hinge", "swing", "frame"});

    std::vector<std::string> wallKeys;
    for (auto it = j["floors"][0]["walls"][0].begin(); it != j["floors"][0]["walls"][0].end(); ++it) wallKeys.push_back(it.key());
    REQUIRE(wallKeys == std::vector<std::string>{"id", "from", "to", "thickness", "height", "bulge", "material_inside", "material_outside"});
}

TEST_CASE("missing optional fields take their defaults", "[core][json]") {
    const PlanRead r = readPlanText(R"({
        "format":"plynth-plan","version":2,
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
    REQUIRE(f.openings[0].hinge == HingeEnd::Start);
    REQUIRE(f.openings[0].swing == WallSide::Right);
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
        j["version"] = 3;
        const PlanRead r = planFromJson(j);
        REQUIRE(r.status.code == ErrorCode::UnsupportedVersion);
        REQUIRE(r.fileVersion == 3);
    }
    SECTION("a bad opening type, hinge or swing") {
        Json j = planToJson(fullPlan());
        j["floors"][0]["openings"][0]["type"] = "gate";
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/openings/0/type");
        j = planToJson(fullPlan());
        j["floors"][0]["openings"][0]["hinge"] = "middle";
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/openings/0/hinge");
        j = planToJson(fullPlan());
        j["floors"][0]["openings"][0]["swing"] = 1;
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/openings/0/swing");
        j = planToJson(fullPlan());
        j["floors"][0]["openings"][0]["swing"] = "up";
        expectPlanRefused(j, ErrorCode::BadFile, "/floors/0/openings/0/swing");
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
    REQUIRE(keys == std::vector<std::string>{"format", "version", "name", "export_mode", "grid_size", "defaults"});

    const SettingsRead r = settingsFromJson(Json::parse(R"({"format":"plynth-project","version":2,"name":"A"})"));
    plynth::test::requireOk(r.status);
    REQUIRE(r.settings.exportMode == ExportMode::Single);
    REQUIRE(r.settings.gridSize == kDefaultGridSize);
    REQUIRE(r.settings.defaults == DefaultSizes{});
    REQUIRE_FALSE(r.migrated);
}

TEST_CASE("settings refuse bad values", "[core][json]") {
    const auto refused = [](const char* text, ErrorCode code, const std::string& where) {
        const SettingsRead r = settingsFromJson(Json::parse(text));
        INFO(text << " -> " << r.status.message);
        REQUIRE(r.status.code == code);
        REQUIRE(r.status.where == where);
    };
    refused(R"({"format":"plynth-project","version":2})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":2,"name":""})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":2,"name":5})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":2,"name":"A","export_mode":"double"})", ErrorCode::BadFile, "/export_mode");
    refused(R"({"format":"plynth-project","version":2,"name":"A","grid_size":0})", ErrorCode::BadFile, "/grid_size");
    refused(R"({"format":"plynth-project","version":2,"name":"A","grid_size":null})", ErrorCode::BadFile, "/grid_size");
    refused(R"({"format":"plynth-project","version":2,"name":"A","extra":1})", ErrorCode::BadFile, "/extra");
    refused(R"({"format":"plynth-plan","version":2,"name":"A"})", ErrorCode::BadFile, "/format");
    refused(R"({"format":"plynth-project","name":"A"})", ErrorCode::BadFile, "/version");
    refused(R"({"format":"plynth-project","version":3,"name":"A"})", ErrorCode::UnsupportedVersion, "/version");
    refused(R"({"format":"plynth-project","version":2,"name":"A","defaults":{"extra":1}})", ErrorCode::BadFile, "/defaults/extra");
    refused(R"({"format":"plynth-project","version":2,"name":"A","defaults":{"door_width":0}})", ErrorCode::BadFile, "/defaults");
    refused(R"({"format":"plynth-project","version":2,"name":"A","defaults":{"door_width":"wide"}})", ErrorCode::BadFile,
            "/defaults/door_width");
    refused(R"({"format":"plynth-project","version":2,"name":"A","defaults":{"ceiling_height":2.0}})", ErrorCode::BadFile, "/defaults");
    refused(R"({"format":"plynth-project","version":2,"name":"A","defaults":[1]})", ErrorCode::BadFile, "/defaults");
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

TEST_CASE("the current format versions are 2", "[core][json][migrate]") {
    REQUIRE(kPlanFormatVersion == 2);
    REQUIRE(kProjectFormatVersion == 2);
    REQUIRE(planMigrations().size() == 1);
    REQUIRE(planMigrations()[0].fromVersion == 1);
    REQUIRE(projectMigrations().size() == 1);
    REQUIRE(projectMigrations()[0].fromVersion == 1);
}

TEST_CASE("a version 1 plan upgrades with hinge start and swing right", "[core][json][migrate]") {
    const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "v1-full.json");
    const PlanRead r = planFromJson(Json::parse(text));
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 1);
    REQUIRE(r.migrated);
    REQUIRE_FALSE(r.plan.floors[0].openings.empty());
    for (const Opening& o : r.plan.floors[0].openings) {
        REQUIRE(o.hinge == HingeEnd::Start);
        REQUIRE(o.swing == WallSide::Right);
    }
    const Json written = planToJson(r.plan);
    REQUIRE(written["version"] == 2);
    REQUIRE(written["floors"][0]["openings"][0]["hinge"] == "start");
    REQUIRE(written["floors"][0]["openings"][0]["swing"] == "right");
}

TEST_CASE("a version 1 file with hinge and swing already present keeps them", "[core][json][migrate]") {
    Json j = Json::parse(R"({"format":"plynth-plan","version":1,"floors":[{"id":"f1",
        "points":[{"id":"p1","x":0,"y":0},{"id":"p2","x":4,"y":0}],"walls":[{"id":"w1","from":"p1","to":"p2"}],
        "openings":[{"id":"o1","wall":"w1","offset":2,"type":"door","width":0.9,"height":2.1,"hinge":"end","swing":"left"}]}]})");
    const PlanRead r = planFromJson(j);
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan.floors[0].openings[0].hinge == HingeEnd::End);
    REQUIRE(r.plan.floors[0].openings[0].swing == WallSide::Left);
}

TEST_CASE("a plan with hinge end and swing left round trips", "[core][json]") {
    Plan plan = fullPlan();
    plan.floors[0].openings[0].hinge = HingeEnd::End;
    plan.floors[0].openings[0].swing = WallSide::Left;
    const PlanRead r = planFromJson(Json::parse(planToJson(plan).dump()));
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan == plan);
    REQUIRE(r.plan.floors[0].openings[0].hinge == HingeEnd::End);
    REQUIRE(r.plan.floors[0].openings[0].swing == WallSide::Left);
}

TEST_CASE("a version 1 project gets the built-in defaults", "[core][json][migrate]") {
    const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "v1-project.json");
    const SettingsRead r = settingsFromJson(Json::parse(text));
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 1);
    REQUIRE(r.migrated);
    REQUIRE(r.settings.name == "Old flat");
    REQUIRE(r.settings.gridSize == 0.25);
    REQUIRE(r.settings.defaults == DefaultSizes{});
    REQUIRE(settingsToJson(r.settings)["version"] == 2);
}

TEST_CASE("project defaults round trip", "[core][json]") {
    ProjectSettings s;
    s.defaults.ceilingHeight = 2.6;
    s.defaults.doorWidth = 0.8;
    s.defaults.doorHeight = 2.0;
    s.defaults.windowWidth = 1.0;
    s.defaults.windowHeight = 1.1;
    s.defaults.windowSill = 0.8;
    const SettingsRead r = settingsFromJson(Json::parse(settingsToJson(s).dump()));
    plynth::test::requireOk(r.status);
    REQUIRE(r.settings == s);
}

TEST_CASE("the wall side and hinge end names", "[core]") {
    REQUIRE(wallSideName(WallSide::Left) == "left");
    REQUIRE(wallSideName(WallSide::Right) == "right");
    REQUIRE(wallSideFromName("left") == WallSide::Left);
    REQUIRE_FALSE(wallSideFromName("up"));
    REQUIRE(otherSide(WallSide::Left) == WallSide::Right);
    REQUIRE(otherSide(WallSide::Right) == WallSide::Left);
    REQUIRE(hingeEndName(HingeEnd::Start) == "start");
    REQUIRE(hingeEndName(HingeEnd::End) == "end");
    REQUIRE(hingeEndFromName("end") == HingeEnd::End);
    REQUIRE_FALSE(hingeEndFromName("middle"));
    REQUIRE(otherEnd(HingeEnd::Start) == HingeEnd::End);
    REQUIRE(otherEnd(HingeEnd::End) == HingeEnd::Start);
}
