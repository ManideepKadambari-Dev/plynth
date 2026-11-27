#include "OlderFormats.h"
#include "SchemaSupport.h"
#include "SquareFixtures.h"

#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <limits>
#include <string>

using namespace plynth;
using plynth::test::schemaProblem;

namespace {

// Turns ordered JSON into the plain kind the validator takes.
nlohmann::json plain(const Json& j)
{
    return nlohmann::json::parse(j.dump());
}

// The square with room r1, a door and two light markers.
Plan litPlan()
{
    Plan plan = makeEmptyPlan();
    Floor f = test::squareFloorWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    r.dressing = false;
    f.rooms = {r};
    Opening door = test::doorOn("w1", 2.0);
    door.id = "o1";
    door.autoFrame = false;
    f.openings = {door};
    LightMarker ceiling;
    ceiling.id = "l1";
    ceiling.room = "r1";
    ceiling.x = 2.0;
    ceiling.y = 2.0;
    ceiling.z = 2.9;
    LightMarker lamp;
    lamp.id = "l2";
    lamp.kind = LightKind::Standing;
    lamp.x = 6.0;
    lamp.y = 1.0;
    lamp.z = 1.4;
    f.lights = {ceiling, lamp};
    plan.floors[0] = f;
    return plan;
}

// Settings with every finishing field away from its default.
ProjectSettings finishSettings()
{
    ProjectSettings s;
    s.name = "Lit";
    s.trim.casing.enabled = false;
    s.trim.skirting.profile = "square";
    s.trim.skirting.height = 0.12;
    s.trim.railingHeight = 1.0;
    s.variation.seed = 4294967295u;
    s.variation.furniture = false;
    setMaterialDefault(s.materials, Surface::Trim, "oak_trim");
    setMaterialDefault(s.materials, Surface::Railing, std::nullopt);
    return s;
}

// Removes a key from every object of a list.
void dropFromEach(Json& list, const char* key)
{
    for (Json& entry : list) entry.erase(key);
}

}

TEST_CASE("plan 7 keeps lights, dressing and automatic frames", "[core][json][finish]")
{
    const Plan plan = litPlan();
    test::requirePlanValid(plan);
    const Json json = planToJson(plan);
    CHECK(json["version"] == 8);
    CHECK(json["floors"][0]["lights"].size() == 2);
    CHECK(json["floors"][0]["lights"][1]["kind"] == "standing");
    CHECK(json["floors"][0]["lights"][1]["room"].is_null());
    CHECK(json["floors"][0]["rooms"][0]["dressing"] == false);
    CHECK(json["floors"][0]["openings"][0]["auto_frame"] == false);
    CHECK(schemaProblem("plan.schema.json", plain(json)) == "");
    CHECK(schemaProblem("plan.v6.schema.json", plain(json)) != "");
    const PlanRead read = planFromJson(json);
    test::requireOk(read.status);
    CHECK(read.plan == plan);
    CHECK_FALSE(read.migrated);
}

TEST_CASE("a version 6 plan opens at 7 with the finishing defaults", "[core][json][finish][migrate]")
{
    Json json = planToJson(litPlan());
    test::planBackTo7(json);
    json["version"] = 6;
    Json& floor = json["floors"][0];
    floor.erase("lights");
    dropFromEach(floor["rooms"], "dressing");
    dropFromEach(floor["openings"], "auto_frame");
    CHECK(schemaProblem("plan.v6.schema.json", plain(json)) == "");
    const PlanRead read = planFromJson(json);
    test::requireOk(read.status);
    CHECK(read.fileVersion == 6);
    CHECK(read.migrated);
    CHECK(read.plan.floors[0].lights.empty());
    CHECK(read.plan.floors[0].rooms[0].dressing);
    CHECK(read.plan.floors[0].openings[0].autoFrame);
    CHECK(planToJson(read.plan)["version"] == 8);

    Json newer = planToJson(litPlan());
    newer["version"] = 9;
    CHECK(planFromJson(newer).status.code == ErrorCode::UnsupportedVersion);
}

TEST_CASE("every light rule of the plan check", "[core][json][finish]")
{
    auto code = [](auto change) {
        Plan plan = litPlan();
        change(plan.floors[0].lights[0], plan);
        return checkPlan(plan);
    };
    CHECK(code([](LightMarker&, Plan&) {}).ok());
    CHECK(code([](LightMarker& l, Plan&) { l.z = 0.0; }).ok());
    CHECK(code([](LightMarker& l, Plan&) { l.z = 3.0; }).ok());
    CHECK(code([](LightMarker& l, Plan&) { l.z = 3.01; }).code == ErrorCode::InvalidValue);
    CHECK(code([](LightMarker& l, Plan&) { l.z = -0.01; }).code == ErrorCode::InvalidValue);
    CHECK(code([](LightMarker& l, Plan&) { l.x = std::numeric_limits<double>::infinity(); }).code == ErrorCode::InvalidValue);
    const Status room = code([](LightMarker& l, Plan&) { l.room = "r9"; });
    CHECK(room.code == ErrorCode::NotFound);
    CHECK(room.where == "/floors/0/lights/0/room");
    CHECK_FALSE(code([](LightMarker& l, Plan&) { l.id = "w1"; }).ok());
    CHECK_FALSE(code([](LightMarker& l, Plan&) { l.id = "1l"; }).ok());

    const Plan plan = litPlan();
    CHECK(findLight(plan.floors[0], "l2") != nullptr);
    CHECK(findFloorContaining(plan, "l1") == &plan.floors[0]);
    CHECK(idInUse(plan, "l2"));
    const Id fresh = newId(plan, kLightIdPrefix);
    CHECK(fresh.front() == 'l');
    CHECK(isValidId(fresh));
    CHECK_FALSE(idInUse(plan, fresh));

    Json bad = planToJson(litPlan());
    bad["floors"][0]["lights"][0]["kind"] = "spot";
    CHECK(planFromJson(bad).status.code == ErrorCode::BadFile);
    Json noZ = planToJson(litPlan());
    noZ["floors"][0]["lights"][0].erase("z");
    CHECK(planFromJson(noZ).status.code == ErrorCode::BadFile);
    Json flag = planToJson(litPlan());
    flag["floors"][0]["rooms"][0]["dressing"] = "no";
    CHECK(planFromJson(flag).status.code == ErrorCode::BadFile);
}

TEST_CASE("project 6 keeps trim, variation and the trim and railing materials", "[core][json][finish]")
{
    const ProjectSettings s = finishSettings();
    const Json json = settingsToJson(s);
    CHECK(json["version"] == 7);
    CHECK(json["trim"]["casing"]["enabled"] == false);
    CHECK(json["trim"]["railing_height"] == 1.0);
    CHECK(json["variation"]["seed"] == 4294967295u);
    CHECK(json["materials"]["trim"] == "oak_trim");
    CHECK(json["materials"]["railing"].is_null());
    CHECK(schemaProblem("project.schema.json", plain(json)) == "");
    CHECK(schemaProblem("project.v5.schema.json", plain(json)) != "");
    const SettingsRead read = settingsFromJson(json);
    test::requireOk(read.status);
    CHECK(read.settings == s);

    Json badSeed = json;
    badSeed["variation"]["seed"] = -1;
    CHECK(settingsFromJson(badSeed).status.code == ErrorCode::BadFile);
    Json badSize = json;
    badSize["trim"]["sill"]["depth"] = 0.6;
    CHECK(settingsFromJson(badSize).status.code == ErrorCode::BadFile);
    Json unknown = json;
    unknown["trim"]["frieze"] = Json::object();
    CHECK(settingsFromJson(unknown).status.code == ErrorCode::BadFile);
}

TEST_CASE("a version 5 project opens at 6 with the finishing defaults", "[core][json][finish][migrate]")
{
    Json json = settingsToJson(ProjectSettings{});
    test::projectBackTo6(json);
    json["version"] = 5;
    json.erase("trim");
    json.erase("variation");
    json["materials"].erase("trim");
    json["materials"].erase("railing");
    CHECK(schemaProblem("project.v5.schema.json", plain(json)) == "");
    const SettingsRead read = settingsFromJson(json);
    test::requireOk(read.status);
    CHECK(read.migrated);
    CHECK(read.settings.trim == TrimSettings{});
    CHECK(read.settings.variation == VariationSettings{});
    CHECK(read.settings.materials.trim == std::optional<std::string>("painted_wood"));
    CHECK(read.settings.materials.railing == std::optional<std::string>("brushed_steel"));

    Json newer = settingsToJson(ProjectSettings{});
    newer["version"] = 8;
    CHECK(settingsFromJson(newer).status.code == ErrorCode::UnsupportedVersion);
}
