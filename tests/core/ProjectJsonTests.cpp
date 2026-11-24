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

namespace {

// Reads a plan from JSON text.
PlanRead readPlanText(const std::string& text)
{
    return planFromJson(Json::parse(text));
}

// A tiny valid plan file as JSON, ready to be broken by a test.
Json minimalPlanJson()
{
    return Json::parse(R"({"format":"plynth-plan","version":6,"floors":[{"id":"f1"}]})");
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
    REQUIRE(r.fileVersion == 6);
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
    REQUIRE(top == std::vector<std::string>{"format", "version", "scale", "floors", "roof", "roof_sections"});

    std::vector<std::string> floorKeys;
    for (auto it = j["floors"][0].begin(); it != j["floors"][0].end(); ++it) floorKeys.push_back(it.key());
    REQUIRE(floorKeys == std::vector<std::string>{"id", "name", "image", "ceiling_height", "points", "walls", "rooms", "openings", "items",
                                                    "slab_thickness", "image_offset", "stairs"});

    std::vector<std::string> openingKeys;
    for (auto it = j["floors"][0]["openings"][0].begin(); it != j["floors"][0]["openings"][0].end(); ++it) openingKeys.push_back(it.key());
    REQUIRE(openingKeys == std::vector<std::string>{"id", "wall", "offset", "type", "shape", "width", "height", "sill", "hinge", "swing", "frame"});

    std::vector<std::string> wallKeys;
    for (auto it = j["floors"][0]["walls"][0].begin(); it != j["floors"][0]["walls"][0].end(); ++it) wallKeys.push_back(it.key());
    REQUIRE(wallKeys == std::vector<std::string>{"id", "from", "to", "thickness", "height", "bulge", "material_inside", "material_outside"});
}

TEST_CASE("missing optional fields take their defaults", "[core][json]") {
    const PlanRead r = readPlanText(R"({
        "format":"plynth-plan","version":3,
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
        j["version"] = 7;
        const PlanRead r = planFromJson(j);
        REQUIRE(r.status.code == ErrorCode::UnsupportedVersion);
        REQUIRE(r.fileVersion == 7);
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
    REQUIRE(keys == std::vector<std::string>{"format", "version", "name", "export_mode", "grid_size", "defaults", "materials"});

    const SettingsRead r = settingsFromJson(Json::parse(R"({"format":"plynth-project","version":5,"name":"A"})"));
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
    refused(R"({"format":"plynth-project","version":5})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":5,"name":""})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":5,"name":5})", ErrorCode::BadFile, "/name");
    refused(R"({"format":"plynth-project","version":5,"name":"A","export_mode":"double"})", ErrorCode::BadFile, "/export_mode");
    refused(R"({"format":"plynth-project","version":5,"name":"A","grid_size":0})", ErrorCode::BadFile, "/grid_size");
    refused(R"({"format":"plynth-project","version":5,"name":"A","grid_size":null})", ErrorCode::BadFile, "/grid_size");
    refused(R"({"format":"plynth-project","version":5,"name":"A","extra":1})", ErrorCode::BadFile, "/extra");
    refused(R"({"format":"plynth-plan","version":5,"name":"A"})", ErrorCode::BadFile, "/format");
    refused(R"({"format":"plynth-project","name":"A"})", ErrorCode::BadFile, "/version");
    refused(R"({"format":"plynth-project","version":6,"name":"A"})", ErrorCode::UnsupportedVersion, "/version");
    refused(R"({"format":"plynth-project","version":5,"name":"A","defaults":{"extra":1}})", ErrorCode::BadFile, "/defaults/extra");
    refused(R"({"format":"plynth-project","version":5,"name":"A","defaults":{"door_width":0}})", ErrorCode::BadFile, "/defaults");
    refused(R"({"format":"plynth-project","version":5,"name":"A","defaults":{"door_width":"wide"}})", ErrorCode::BadFile,
            "/defaults/door_width");
    refused(R"({"format":"plynth-project","version":5,"name":"A","defaults":{"ceiling_height":2.0}})", ErrorCode::BadFile, "/defaults");
    refused(R"({"format":"plynth-project","version":5,"name":"A","defaults":[1]})", ErrorCode::BadFile, "/defaults");
}

TEST_CASE("a bad default material name is refused with a pointer", "[core][json][materials]") {
    const SettingsRead r = settingsFromJson(
        Json::parse(R"({"format":"plynth-project","version":5,"name":"A","materials":{"floor":"bad name"}})"));
    REQUIRE(r.status.code == ErrorCode::BadFile);
    REQUIRE(r.status.where == "/materials/floor");

    const SettingsRead unknown = settingsFromJson(
        Json::parse(R"({"format":"plynth-project","version":5,"name":"A","materials":{"roofing":"tile"}})"));
    REQUIRE(unknown.status.code == ErrorCode::BadFile);
    REQUIRE(unknown.status.where == "/materials/roofing");

    const SettingsRead wrongType = settingsFromJson(
        Json::parse(R"({"format":"plynth-project","version":5,"name":"A","materials":{"floor":5}})"));
    REQUIRE(wrongType.status.code == ErrorCode::BadFile);
    REQUIRE(wrongType.status.where == "/materials/floor");
}

TEST_CASE("settings write every surface and read them back", "[core][json][materials]") {
    ProjectSettings s;
    setMaterialDefault(s.materials, Surface::Ceiling, std::nullopt);
    setMaterialDefault(s.materials, Surface::Floor, "grey_tile");
    const Json j = settingsToJson(s);
    REQUIRE(j["version"] == 6);
    REQUIRE(j["materials"].size() == 14);
    REQUIRE(j["materials"]["ceiling"].is_null());
    REQUIRE(j["materials"]["floor"] == "grey_tile");
    std::vector<std::string> keys;
    for (auto it = j["materials"].begin(); it != j["materials"].end(); ++it) keys.push_back(it.key());
    REQUIRE(keys == std::vector<std::string>{"wall_inside", "wall_outside", "wall_top", "floor", "ceiling", "door", "frame", "glass", "handle",
                                                    "roof", "flat_roof", "stair"});
    const SettingsRead r = settingsFromJson(j);
    plynth::test::requireOk(r.status);
    REQUIRE(r.settings == s);
    REQUIRE_FALSE(r.migrated);
}

TEST_CASE("a version 3 project gets the built-in default materials", "[core][json][migrate][materials]") {
    const SettingsRead r = settingsFromJson(Json::parse(R"({"format":"plynth-project","version":3,"name":"A"})"));
    plynth::test::requireOk(r.status);
    REQUIRE(r.migrated);
    REQUIRE(r.settings.materials == MaterialDefaults{});
    REQUIRE(r.settings.materials.wallInside == "white_paint");
    REQUIRE(r.settings.materials.handle == "brushed_steel");
}

TEST_CASE("room wall material round trips and version 3 rooms get none", "[core][json][migrate][materials]") {
    Plan plan = fullPlan();
    REQUIRE_FALSE(plan.floors[0].rooms.empty());
    plan.floors[0].rooms[0].wallMaterial = "tile_blue";
    const PlanRead back = planFromJson(planToJson(plan));
    plynth::test::requireOk(back.status);
    REQUIRE(back.plan.floors[0].rooms[0].wallMaterial == "tile_blue");
    const Json written = planToJson(plan);
    REQUIRE(written["floors"][0]["rooms"][0].contains("wall_material"));

    Json old = planToJson(plan);
    old["version"] = 3;
    for (Json& floor : old["floors"]) {
        for (Json& room : floor["rooms"]) room.erase("wall_material");
    }
    const PlanRead migrated = planFromJson(old);
    plynth::test::requireOk(migrated.status);
    REQUIRE(migrated.migrated);
    for (const Floor& floor : migrated.plan.floors) {
        for (const Room& room : floor.rooms) REQUIRE_FALSE(room.wallMaterial.has_value());
    }

    Json bad = planToJson(plan);
    bad["floors"][0]["rooms"][0]["wall_material"] = "bad name";
    const PlanRead refused = planFromJson(bad);
    REQUIRE(refused.status.code == ErrorCode::BadFile);
    REQUIRE(refused.status.where == "/floors/0/rooms/0/wall_material");
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

TEST_CASE("the current format versions are 7 and 6", "[core][json][migrate]") {
    REQUIRE(kPlanFormatVersion == 7);
    REQUIRE(kProjectFormatVersion == 6);
    REQUIRE(planMigrations().size() == 6);
    REQUIRE(planMigrations()[0].fromVersion == 1);
    REQUIRE(planMigrations()[1].fromVersion == 2);
    REQUIRE(planMigrations()[2].fromVersion == 3);
    REQUIRE(planMigrations()[3].fromVersion == 4);
    REQUIRE(planMigrations()[4].fromVersion == 5);
    REQUIRE(planMigrations()[5].fromVersion == 6);
    REQUIRE(projectMigrations().size() == 5);
    REQUIRE(projectMigrations()[0].fromVersion == 1);
    REQUIRE(projectMigrations()[1].fromVersion == 2);
    REQUIRE(projectMigrations()[2].fromVersion == 3);
    REQUIRE(projectMigrations()[3].fromVersion == 4);
    REQUIRE(projectMigrations()[4].fromVersion == 5);
}

TEST_CASE("a version 4 plan reads with the new fields at their defaults", "[core][json][migrate]") {
    const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "v4-two-floors.json");
    const PlanRead r = planFromJson(Json::parse(text));
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 4);
    REQUIRE(r.migrated);
    REQUIRE(r.plan.floors.size() == 2);
    for (const Floor& f : r.plan.floors) {
        REQUIRE(f.slabThickness == 0.2);
        REQUIRE(f.imageOffset == ImageOffset{});
        REQUIRE(f.stairs.empty());
    }
    REQUIRE(r.plan.roof == RoofSettings{});
    REQUIRE(r.plan.roofSections.empty());
    const Json written = planToJson(r.plan);
    REQUIRE(written["version"] == 7);
    REQUIRE(written["floors"][1]["slab_thickness"] == 0.2);
    REQUIRE(written["roof"]["type"] == "flat");
    REQUIRE(written["roof"]["direction"].is_null());
    REQUIRE(written["roof_sections"].is_array());
}

TEST_CASE("a version 4 project gets the roof, flat roof and stair defaults", "[core][json][migrate]") {
    const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "v4-project.json");
    const SettingsRead r = settingsFromJson(Json::parse(text));
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 4);
    REQUIRE(r.migrated);
    REQUIRE(r.settings.materials.wallInside == "plaster_white");
    REQUIRE(r.settings.materials.roof == "roof_tiles");
    REQUIRE(r.settings.materials.flatRoof == "concrete");
    REQUIRE(r.settings.materials.stair == "oak_planks");
    const Json written = settingsToJson(r.settings);
    REQUIRE(written["version"] == 6);
    REQUIRE(written["materials"]["stair"] == "oak_planks");

    const SettingsRead minimal = settingsFromJson(Json::parse(
        plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "v4-project-minimal.json")));
    plynth::test::requireOk(minimal.status);
    REQUIRE(minimal.settings.materials == MaterialDefaults{});
}

TEST_CASE("floors with stairs and a plan with roof entries keep every field", "[core][json]") {
    Plan plan = plynth::test::fullPlan();
    plan.floors[0].slabThickness = 0.25;
    plan.floors[1].imageOffset = {1.5, -0.25};
    Stair st;
    st.id = "s1";
    st.type = "u_shape";
    st.x = 1.0;
    st.y = 2.0;
    st.rotation = 90.0;
    st.width = 1.2;
    st.turn = StairTurn::Left;
    st.material = "oak_planks";
    plan.floors[0].stairs.push_back(st);
    plan.roof.type = "gable";
    plan.roof.direction = 45.0;
    RoofSection rs;
    rs.id = "rf1";
    rs.floor = "f2";
    rs.x = 1.0;
    rs.y = 1.0;
    rs.settings = {"hip", 35.0, 0.4, 0.0, std::nullopt};
    rs.material = "roof_tiles";
    plan.roofSections.push_back(rs);
    const PlanRead r = planFromJson(Json::parse(planToJson(plan).dump()));
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan == plan);
    REQUIRE_FALSE(r.migrated);
    const Json j = planToJson(plan);
    std::vector<std::string> keys;
    for (auto it = j["floors"][0]["stairs"][0].begin(); it != j["floors"][0]["stairs"][0].end(); ++it) keys.push_back(it.key());
    REQUIRE(keys == std::vector<std::string>{"id", "type", "x", "y", "rotation", "width", "turn", "material"});
}

TEST_CASE("a version 2 plan upgrades with empty item lists", "[core][json][migrate]") {
    const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "v2-full.json");
    const PlanRead r = planFromJson(Json::parse(text));
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 2);
    REQUIRE(r.migrated);
    for (const Floor& f : r.plan.floors) REQUIRE(f.items.empty());
    const Json written = planToJson(r.plan);
    REQUIRE(written["version"] == 7);
    REQUIRE(written["floors"][0]["items"].is_array());
    REQUIRE(written["floors"][0]["items"].empty());
    REQUIRE(written["floors"][1]["items"].empty());
}

TEST_CASE("a version 2 project gets the wall item centre", "[core][json][migrate]") {
    const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "v2-project.json");
    const SettingsRead r = settingsFromJson(Json::parse(text));
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 2);
    REQUIRE(r.migrated);
    REQUIRE(r.settings.defaults.ceilingHeight == 2.6);
    REQUIRE(r.settings.defaults.wallItemCentre == 1.2);
    const Json written = settingsToJson(r.settings);
    REQUIRE(written["version"] == 6);
    REQUIRE(written["defaults"]["wall_item_centre"] == 1.2);
}

TEST_CASE("items round trip and are written in schema order", "[core][json]") {
    const Plan plan = fullPlan();
    REQUIRE(plan.floors[0].items.size() == 2);
    const Json j = planToJson(plan);
    std::vector<std::string> keys;
    for (auto it = j["floors"][0]["items"][0].begin(); it != j["floors"][0]["items"][0].end(); ++it) keys.push_back(it.key());
    REQUIRE(keys == std::vector<std::string>{"id", "asset", "room", "x", "y", "z", "rotation", "wall", "size", "generator"});
    REQUIRE(j["floors"][0]["items"][0]["wall"].is_null());
    REQUIRE(j["floors"][0]["items"][1]["wall"] == "w3");
    const PlanRead r = planFromJson(Json::parse(j.dump()));
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan == plan);
    REQUIRE(r.plan.floors[0].items[1].wall == "w3");
    REQUIRE(r.plan.floors[0].items[0].size == ItemSize{2.0, 0.9, 0.85});
}

TEST_CASE("the reader refuses broken items with a pointer", "[core][json]") {
    const auto withItem = [](const std::string& item) {
        return Json::parse(R"({"format":"plynth-plan","version":3,"floors":[{"id":"f1",
            "points":[{"id":"p1","x":0,"y":0},{"id":"p2","x":4,"y":0}],"walls":[{"id":"w1","from":"p1","to":"p2"}],
            "rooms":[{"id":"r1","walls":["w1"]}],"items":[)" + item + "]}]}");
    };
    const std::string size = R"("size":{"width":1,"depth":1,"height":1})";
    plynth::test::requireOk(planFromJson(withItem(R"({"id":"i1","asset":"sofa","x":1,"y":1,)" + size + "}")).status);
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","x":1,"y":1,"rotation":360,)" + size + "}"), ErrorCode::BadFile,
                      "/floors/0/items/0/rotation");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","x":1,"y":1,"z":-0.1,)" + size + "}"), ErrorCode::BadFile,
                      "/floors/0/items/0/z");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","x":1,"y":1,"size":{"width":0,"depth":1,"height":1}})"),
                      ErrorCode::BadFile, "/floors/0/items/0/size/width");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa 1","x":1,"y":1,)" + size + "}"), ErrorCode::BadFile,
                      "/floors/0/items/0/asset");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","room":"r9","x":1,"y":1,)" + size + "}"), ErrorCode::BadFile,
                      "/floors/0/items/0/room");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","wall":"w9","x":1,"y":1,)" + size + "}"), ErrorCode::BadFile,
                      "/floors/0/items/0/wall");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","x":1,"y":1,"colour":1,)" + size + "}"), ErrorCode::BadFile,
                      "/floors/0/items/0/colour");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","x":1,"y":1})"), ErrorCode::BadFile, "/floors/0/items/0/size");
    expectPlanRefused(withItem(R"({"id":"i1","asset":"sofa","y":1,)" + size + "}"), ErrorCode::BadFile,
                      "/floors/0/items/0/x");
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
    REQUIRE(written["version"] == 7);
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
    REQUIRE(settingsToJson(r.settings)["version"] == 6);
}

TEST_CASE("project defaults round trip", "[core][json]") {
    ProjectSettings s;
    s.defaults.ceilingHeight = 2.6;
    s.defaults.doorWidth = 0.8;
    s.defaults.doorHeight = 2.0;
    s.defaults.windowWidth = 1.0;
    s.defaults.windowHeight = 1.1;
    s.defaults.windowSill = 0.8;
    s.defaults.wallItemCentre = 1.4;
    const SettingsRead r = settingsFromJson(Json::parse(settingsToJson(s).dump()));
    plynth::test::requireOk(r.status);
    REQUIRE(r.settings == s);
}

TEST_CASE("a wall item centre above the ceiling is refused", "[core][json]") {
    const auto read = [](const char* defaults) {
        return settingsFromJson(Json::parse(std::string(R"({"format":"plynth-project","version":3,"name":"A","defaults":)") + defaults + "}"));
    };
    plynth::test::requireOk(read(R"({"wall_item_centre":1.4})").status);
    const SettingsRead high = read(R"({"wall_item_centre":3.5})");
    REQUIRE(high.status.code == ErrorCode::BadFile);
    REQUIRE(high.status.where == "/defaults");
    REQUIRE(read(R"({"wall_item_centre":0})").status.code == ErrorCode::BadFile);
    REQUIRE(read(R"({"wall_item_centre":3.0})").status.code == ErrorCode::BadFile);
    REQUIRE(read(R"({"ceiling_height":2.5,"wall_item_centre":2.0})").status.ok());
    REQUIRE(checkDefaultSizes([] {
                DefaultSizes d;
                d.wallItemCentre = 3.5;
                return d;
            }()).code == ErrorCode::InvalidValue);
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

namespace {

// A plan with the full plan's asset items plus a generator item.
Plan planWithGenerator()
{
    Plan plan = fullPlan();
    Item g;
    g.id = "i9";
    g.x = 2.0;
    g.y = 2.0;
    g.size = ItemSize{0.3, 0.3, 2.7};
    GeneratorUse use;
    use.name = "column";
    use.params = {{"diameter", GeneratorValue{0.3}},
                  {"height", GeneratorValue{2.7}},
                  {"material", GeneratorValue{std::string()}},
                  {"plinth", GeneratorValue{true}},
                  {"shape", GeneratorValue{std::string("round")}},
                  {"sides", GeneratorValue{24.0}}};
    g.generator = use;
    plan.floors[0].items.push_back(g);
    return plan;
}

Json jsonWithGenerator(const std::string& generator, const std::string& asset = "null")
{
    return Json::parse(R"({"format":"plynth-plan","version":6,"floors":[{"id":"f1","items":[{"id":"i1","asset":)" + asset +
                       R"(,"x":1,"y":1,"size":{"width":1,"depth":1,"height":1},"generator":)" + generator + "}]}]}");
}

}

TEST_CASE("a generator item round trips and writes null asset", "[core][json][generator]") {
    const Plan plan = planWithGenerator();
    plynth::test::requireOk(checkPlan(plan));
    const Json j = planToJson(plan);
    const Json& item = j["floors"][0]["items"][2];
    REQUIRE(item["asset"].is_null());
    REQUIRE(item["generator"]["name"] == "column");
    REQUIRE(item["generator"]["params"]["material"].is_null());
    REQUIRE(item["generator"]["params"]["plinth"] == true);
    REQUIRE(item["generator"]["params"]["shape"] == "round");
    REQUIRE(j["floors"][0]["items"][0]["generator"].is_null());
    REQUIRE(j["floors"][0]["items"][0]["asset"].is_string());
    const PlanRead r = planFromJson(Json::parse(j.dump()));
    plynth::test::requireOk(r.status);
    REQUIRE(r.plan == plan);
    REQUIRE(isGeneratorItem(r.plan.floors[0].items[2]));
    REQUIRE_FALSE(isGeneratorItem(r.plan.floors[0].items[0]));
}

TEST_CASE("generator params are written sorted", "[core][json][generator]") {
    Plan plan = planWithGenerator();
    auto& params = plan.floors[0].items[2].generator->params;
    std::reverse(params.begin(), params.end());
    const Json j = planToJson(plan);
    std::vector<std::string> keys;
    const Json& written = j["floors"][0]["items"][2]["generator"]["params"];
    for (auto it = written.begin(); it != written.end(); ++it) keys.push_back(it.key());
    REQUIRE(std::is_sorted(keys.begin(), keys.end()));
    REQUIRE(keys.size() == 6);
}

TEST_CASE("generator values read as numbers, flags and text", "[core][json][generator]") {
    const PlanRead r = planFromJson(jsonWithGenerator(R"({"name":"column","params":{"a":2,"b":0.5,"c":false,"d":"x","e":null}})"));
    plynth::test::requireOk(r.status);
    const auto& p = r.plan.floors[0].items[0].generator->params;
    REQUIRE(p.size() == 5);
    REQUIRE(std::get<double>(p[0].value) == 2.0);
    REQUIRE(std::get<double>(p[1].value) == 0.5);
    REQUIRE(std::get<bool>(p[2].value) == false);
    REQUIRE(std::get<std::string>(p[3].value) == "x");
    REQUIRE(std::get<std::string>(p[4].value).empty());
}

TEST_CASE("a generator item with both or neither is refused", "[core][json][generator]") {
    const std::string use = R"({"name":"column","params":{}})";
    expectPlanRefused(jsonWithGenerator(use, "\"sofa\""), ErrorCode::BadFile, "/floors/0/items/0/generator");
    expectPlanRefused(jsonWithGenerator("null"), ErrorCode::BadFile, "/floors/0/items/0/asset");
}

TEST_CASE("a bad generator is refused with a pointer", "[core][json][generator]") {
    expectPlanRefused(jsonWithGenerator(R"({"name":"Bad Name","params":{}})"), ErrorCode::BadFile,
                      "/floors/0/items/0/generator/name");
    expectPlanRefused(jsonWithGenerator(R"({"name":"column","params":{"Bad":1}})"), ErrorCode::BadFile,
                      "/floors/0/items/0/generator/params/Bad");
    expectPlanRefused(jsonWithGenerator(R"({"name":"column","params":{"a":[1]}})"), ErrorCode::BadFile,
                      "/floors/0/items/0/generator/params/a");
    expectPlanRefused(jsonWithGenerator(R"({"name":"column"})"), ErrorCode::BadFile, "/floors/0/items/0/generator/params");
    expectPlanRefused(jsonWithGenerator(R"({"name":"column","params":{},"x":1})"), ErrorCode::BadFile,
                      "/floors/0/items/0/generator/x");
}

TEST_CASE("a version 4 plan upgrades to 6 with no generators", "[core][json][migrate][generator]") {
    for (const char* name : {"v4-full.json", "v4-minimal.json"}) {
        INFO(name);
        const std::string text = plynth::test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / name);
        const PlanRead r = planFromJson(Json::parse(text));
        plynth::test::requireOk(r.status);
        REQUIRE(r.fileVersion == 4);
        REQUIRE(r.migrated);
        for (const Floor& f : r.plan.floors) {
            for (const Item& it : f.items) REQUIRE_FALSE(it.generator.has_value());
        }
        REQUIRE(planToJson(r.plan)["version"] == 6);
    }
}

TEST_CASE("a version 5 plan upgrades to 6", "[core][json][migrate][generator]") {
    Json j = Json::parse(R"({"format":"plynth-plan","version":5,"floors":[{"id":"f1","items":[
        {"id":"i1","asset":"sofa","x":1,"y":1,"size":{"width":1,"depth":1,"height":1}}]}]})");
    const PlanRead r = planFromJson(j);
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 5);
    REQUIRE(r.migrated);
    REQUIRE_FALSE(r.plan.floors[0].items[0].generator.has_value());
}

TEST_CASE("a version 5 plan keeps its slabs, stairs and roofs as version 6", "[core][json][migrate]") {
    Plan plan = fullPlan();
    plan.floors[0].slabThickness = 0.3;
    plan.floors[0].stairs.push_back(Stair{.id = "s1", .type = "l_shape", .x = 1.0, .y = 2.0});
    plan.floors[1].imageOffset = {0.5, -1.0};
    plan.roof.type = "gable";
    RoofSection section;
    section.id = "rf1";
    section.floor = "f2";
    plan.roofSections.push_back(section);

    Json j = planToJson(plan);
    j["version"] = 5;
    for (Json& floor : j["floors"]) {
        for (Json& item : floor["items"]) item.erase("generator");
    }
    const PlanRead r = planFromJson(j);
    plynth::test::requireOk(r.status);
    REQUIRE(r.fileVersion == 5);
    REQUIRE(r.migrated);
    REQUIRE(r.plan == plan);
}
