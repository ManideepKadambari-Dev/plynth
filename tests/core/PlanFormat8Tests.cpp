#include "OlderFormats.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/MaterialDefaults.h>
#include <plynth/core/Materials.h>
#include <plynth/core/Plan.h>
#include <plynth/core/Project.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/Status.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace plynth;
using plynth::test::schemaProblem;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Turns ordered JSON into the plain kind the validator takes.
nlohmann::json plain(const Json& j)
{
    return nlohmann::json::parse(j.dump());
}

// The full plan with one of every object plan format 8 added, each field away from its default.
Plan everyFieldPlan()
{
    Plan plan = test::fullPlan();
    Floor& f1 = plan.floors[0];
    Floor& f2 = plan.floors[1];

    f1.walls[0].cladding.push_back(CladdingZone{"cz1", WallSide::Left, 0.5, 3.0, 0.2, 2.4, 0.03, "cedar_battens"});
    f1.openings[1].fill = "sliding";

    Item rug;
    rug.id = "i3";
    rug.asset = "rug_01";
    rug.x = 1.5;
    rug.y = 1.0;
    rug.size = {2.4, 1.6, 0.01};
    f1.items.push_back(rug);
    f1.items[0].on = "i3";
    f1.items[0].z = 0.01;

    Stair steps;
    steps.id = "s1";
    steps.x = 6.0;
    steps.y = 2.0;
    steps.width = 4.0;
    steps.rise = 0.6;
    steps.cheeks = true;
    steps.railing = false;
    f1.stairs.push_back(steps);

    f1.decks.push_back(Deck{"dk1", {{5.0, 0.0}, {8.0, 0.0}, {8.0, 2.0}, {5.0, 2.0}}, 0.6, 0.2, "oak_planks", false, "painted_wood"});
    f1.railings.push_back(Railing{"rl1", {{5.0, 0.0}, {8.0, 0.0}, {8.0, 2.0}}, true, "dk1", 0.0, 1.1, "glass", "brushed_steel",
                                  "clear_glass"});
    Canopy canopy;
    canopy.id = "cn1";
    canopy.outline = {{5.0, 2.0}, {8.0, 2.0}, {8.0, 4.0}, {5.0, 4.0}};
    canopy.height = 2.5;
    canopy.thickness = 0.3;
    canopy.roof = RoofSettings{"gable", 25.0, 0.2, 0.0, 90.0, 20.0, 0.4, 0.1, 1.5};
    canopy.material = "roof_tiles";
    canopy.undersideMaterial = "white_paint";
    canopy.fasciaMaterial = "painted_wood";
    f1.canopies.push_back(canopy);
    f1.lightStrips.push_back(LightStrip{"ls1", LightStripKind::Cove, {{0.2, 0.2}, {3.8, 0.2}}, 2.5, std::nullopt, 12.0});
    f1.lightStrips.push_back(LightStrip{"ls2", LightStripKind::Step, {}, 0.0, Id("s1"), 4.0});

    f2.dormers.push_back(Dormer{"dm1", 2.0, 1.0, "shed", 2.0, 1.8, 20.0, 1.2, 0.9, 0.3, "red_brick", "roof_tiles"});

    plan.roof = RoofSettings{"flat", 30.0, 0.3, 0.8, std::nullopt, 20.0, 0.4, 0.1, 1.5};
    RoofSection section;
    section.id = "rf1";
    section.floor = "f2";
    section.settings = RoofSettings{"gambrel", 60.0, 0.4, 0.0, std::nullopt, 25.0, 0.3, -0.2, 2.0};
    plan.roofSections.push_back(section);

    plan.groundFloor = "f1";
    plan.site.ground = SiteGround{true, 12.0, "grass"};
    SiteBoundary fence;
    fence.id = "sb1";
    fence.points = {{-2.0, -2.0}, {10.0, -2.0}, {10.0, 8.0}, {-2.0, 8.0}};
    fence.closed = true;
    fence.height = 1.4;
    fence.thickness = 0.08;
    fence.style = "spindles";
    fence.material = "painted_wood";
    fence.gates.push_back(Gate{"g1", 5.0, 1.2, true, "brushed_steel"});
    plan.site.boundaries.push_back(fence);
    SiteBoundary wall;
    wall.id = "sb2";
    wall.kind = BoundaryKind::Wall;
    wall.points = {{-3.0, -3.0}, {11.0, -3.0}};
    wall.height = 2.0;
    wall.thickness = 0.3;
    plan.site.boundaries.push_back(wall);
    plan.site.surfaces.push_back(SiteSurface{"ss1", SiteSurfaceKind::Lawn, {{-1.5, 4.0}, {4.0, 4.0}, {4.0, 7.5}, {-1.5, 7.5}}, 0.02, std::nullopt});
    plan.site.surfaces.push_back(SiteSurface{"ss2", SiteSurfaceKind::Driveway, {{8.5, 4.0}, {9.5, 4.0}, {9.5, 7.5}}, 0.1, "concrete"});
    plan.foundation = Foundation{true, 0.5, 0.05, "concrete"};
    return plan;
}

// Applies a change to the plan with every field and expects checkPlan to refuse it with this code and place.
void refused(const std::function<void(Plan&)>& change, ErrorCode code, const std::string& where)
{
    Plan plan = everyFieldPlan();
    change(plan);
    const Status s = checkPlan(plan);
    INFO("expected " << where << " but got " << s.where << ": " << s.message);
    REQUIRE_FALSE(s.ok());
    CHECK(s.code == code);
    CHECK(s.where == where);
    CHECK_FALSE(s.message.empty());
}

// Applies a change to the plan with every field and expects checkPlan to accept it.
void accepted(const std::function<void(Plan&)>& change)
{
    Plan plan = everyFieldPlan();
    change(plan);
    const Status s = checkPlan(plan);
    INFO(s.message << " at " << s.where);
    CHECK(s.ok());
}

// The keys of a JSON object in the order they were written.
std::vector<std::string> keysOf(const Json& object)
{
    std::vector<std::string> keys;
    for (auto it = object.begin(); it != object.end(); ++it) keys.push_back(it.key());
    return keys;
}

// Reads a data file of the tests as JSON.
Json dataFile(const char* name)
{
    return Json::parse(test::readTextFile(std::filesystem::path(PLYNTH_TEST_DATA_DIR) / name));
}

}

TEST_CASE("a plan with every format 8 field is valid, matches the schema and round trips", "[core][plan][format8]")
{
    const Plan plan = everyFieldPlan();
    test::requirePlanValid(plan);
    const Json j = planToJson(plan);
    CHECK(j["version"] == 8);
    CHECK(schemaProblem("plan.schema.json", plain(j)) == "");
    CHECK(schemaProblem("plan.v7.schema.json", plain(j)) != "");

    const PlanRead read = planFromJson(Json::parse(j.dump(2)));
    test::requireOk(read.status);
    CHECK(read.plan == plan);
    CHECK(read.fileVersion == 8);
    CHECK_FALSE(read.migrated);
}

TEST_CASE("format 8 fields are written in schema order with their values", "[core][plan][format8]")
{
    const Json j = planToJson(everyFieldPlan());
    CHECK(keysOf(j) == std::vector<std::string>{"format", "version", "scale", "floors", "roof", "roof_sections", "ground_floor", "site",
                                                "foundation"});
    const Json& f1 = j["floors"][0];
    CHECK(keysOf(f1["walls"][0]["cladding"][0])
          == std::vector<std::string>{"id", "side", "start", "end", "bottom", "top", "depth", "material"});
    CHECK(f1["walls"][0]["cladding"][0]["side"] == "left");
    CHECK(f1["walls"][1]["cladding"].empty());
    CHECK(f1["openings"][1]["fill"] == "sliding");
    CHECK(f1["openings"][0]["fill"].is_null());
    CHECK(f1["items"][0]["on"] == "i3");
    CHECK(f1["items"][1]["on"].is_null());
    CHECK(f1["stairs"][0]["rise"] == 0.6);
    CHECK(f1["stairs"][0]["cheeks"] == true);
    CHECK(f1["stairs"][0]["railing"] == false);
    CHECK(keysOf(f1["decks"][0]) == std::vector<std::string>{"id", "outline", "top", "thickness", "material", "skirt", "skirt_material"});
    CHECK(f1["decks"][0]["outline"][1] == Json{{"x", 8.0}, {"y", 0.0}});
    CHECK(keysOf(f1["railings"][0])
          == std::vector<std::string>{"id", "points", "closed", "host", "base", "height", "style", "material", "glass_material"});
    CHECK(f1["railings"][0]["host"] == "dk1");
    CHECK(keysOf(f1["canopies"][0])
          == std::vector<std::string>{"id", "outline", "height", "thickness", "roof", "material", "underside_material", "fascia_material"});
    CHECK(f1["canopies"][0]["roof"]["type"] == "gable");
    CHECK(keysOf(f1["light_strips"][1]) == std::vector<std::string>{"id", "kind", "points", "z", "stair", "power"});
    CHECK(f1["light_strips"][1]["kind"] == "step");
    CHECK(f1["light_strips"][1]["points"].empty());
    CHECK(keysOf(j["floors"][1]["dormers"][0])
          == std::vector<std::string>{"id", "x", "y", "type", "width", "depth", "pitch", "window_width", "window_height", "window_sill",
                                      "material", "roof_material"});
    CHECK(keysOf(j["roof"])
          == std::vector<std::string>{"type", "pitch", "overhang", "parapet", "direction", "second_pitch", "break_ratio", "ridge_offset", "rise"});
    CHECK(j["roof_sections"][0]["settings"]["ridge_offset"] == -0.2);
    CHECK(j["ground_floor"] == "f1");
    CHECK(keysOf(j["site"]) == std::vector<std::string>{"ground", "boundaries", "surfaces"});
    CHECK(j["site"]["ground"] == Json{{"enabled", true}, {"margin", 12.0}, {"material", "grass"}});
    CHECK(keysOf(j["site"]["boundaries"][0])
          == std::vector<std::string>{"id", "kind", "points", "closed", "height", "thickness", "style", "material", "gates"});
    CHECK(j["site"]["boundaries"][1]["kind"] == "wall");
    CHECK(keysOf(j["site"]["boundaries"][0]["gates"][0]) == std::vector<std::string>{"id", "offset", "width", "sliding", "material"});
    CHECK(j["site"]["surfaces"][0]["kind"] == "lawn");
    CHECK(j["site"]["surfaces"][1]["kind"] == "driveway");
    CHECK(j["foundation"] == Json{{"enabled", true}, {"height", 0.5}, {"step_out", 0.05}, {"material", "concrete"}});
}

TEST_CASE("a new plan writes the format 8 defaults", "[core][plan][format8]")
{
    const Json j = planToJson(makeEmptyPlan());
    CHECK(j["ground_floor"].is_null());
    CHECK(j["site"]["ground"]["enabled"] == false);
    CHECK(j["site"]["ground"]["margin"] == 10.0);
    CHECK(j["site"]["boundaries"].empty());
    CHECK(j["site"]["surfaces"].empty());
    CHECK(j["foundation"]["enabled"] == false);
    CHECK(j["foundation"]["height"] == 0.45);
    CHECK(j["foundation"]["step_out"] == 0.03);
    CHECK(j["roof"]["second_pitch"] == 15.0);
    CHECK(j["roof"]["break_ratio"] == 0.5);
    CHECK(j["roof"]["ridge_offset"] == 0.0);
    CHECK(j["roof"]["rise"] == 1.0);
    for (const char* list : {"decks", "railings", "canopies", "dormers", "light_strips"}) {
        INFO(list);
        CHECK(j["floors"][0][list].is_array());
        CHECK(j["floors"][0][list].empty());
    }
    CHECK(schemaProblem("plan.schema.json", plain(j)) == "");
}

TEST_CASE("a version 7 file reads, upgrades and writes as version 8", "[core][plan][format8][migrate]")
{
    const Json file = dataFile("v7-full.json");
    REQUIRE(file["version"] == 7);
    CHECK(schemaProblem("plan.v7.schema.json", plain(file)) == "");
    CHECK(schemaProblem("plan.schema.json", plain(file)) != "");

    const PlanRead read = planFromJson(file);
    test::requireOk(read.status);
    CHECK(read.fileVersion == 7);
    CHECK(read.migrated);
    CHECK_FALSE(read.plan.groundFloor.has_value());
    CHECK(read.plan.site == Site{});
    CHECK(read.plan.foundation == Foundation{});
    CHECK(read.plan.roof.secondPitch == 15.0);
    for (const Floor& floor : read.plan.floors) {
        CHECK(floor.decks.empty());
        CHECK(floor.railings.empty());
        CHECK(floor.canopies.empty());
        CHECK(floor.dormers.empty());
        CHECK(floor.lightStrips.empty());
        for (const Wall& wall : floor.walls) CHECK(wall.cladding.empty());
        for (const Opening& opening : floor.openings) CHECK_FALSE(opening.fill.has_value());
        for (const Item& item : floor.items) CHECK_FALSE(item.on.has_value());
    }

    const Json written = planToJson(read.plan);
    CHECK(written["version"] == 8);
    CHECK(schemaProblem("plan.schema.json", plain(written)) == "");
    CHECK(schemaProblem("plan.v7.schema.json", plain(written)) != "");
    const PlanRead again = planFromJson(written);
    test::requireOk(again.status);
    CHECK_FALSE(again.migrated);
    CHECK(again.plan == read.plan);
}

TEST_CASE("a version 7 plan with stairs and roof entries upgrades to the same plan", "[core][plan][format8][migrate]")
{
    Plan plan = test::fullPlan();
    plan.floors[0].stairs.push_back(Stair{.id = "s1", .type = "u_shape", .x = 1.0, .y = 2.0, .width = 1.4});
    plan.roof.type = "gable";
    RoofSection section;
    section.id = "rf1";
    section.floor = "f2";
    section.settings.type = "hip";
    plan.roofSections.push_back(section);

    Json older = planToJson(plan);
    test::planBackTo7(older);
    CHECK(schemaProblem("plan.v7.schema.json", plain(older)) == "");
    const PlanRead read = planFromJson(older);
    test::requireOk(read.status);
    CHECK(read.migrated);
    CHECK(read.plan == plan);
}

TEST_CASE("the upgrade to version 8 keeps fields that are already there", "[core][plan][format8][migrate]")
{
    Json older = planToJson(everyFieldPlan());
    older["version"] = 7;
    const PlanRead read = planFromJson(older);
    test::requireOk(read.status);
    CHECK(read.migrated);
    CHECK(read.plan == everyFieldPlan());
}

TEST_CASE("the reader refuses broken format 8 fields with a pointer", "[core][plan][format8]")
{
    const auto expectBad = [](const std::function<void(Json&)>& change, const std::string& where) {
        Json j = planToJson(everyFieldPlan());
        change(j);
        const PlanRead r = planFromJson(j);
        INFO(r.status.message << " at " << r.status.where);
        CHECK(r.status.code == ErrorCode::BadFile);
        CHECK(r.status.where == where);
    };
    expectBad([](Json& j) { j["floors"][0]["decks"][0]["colour"] = "red"; }, "/floors/0/decks/0/colour");
    expectBad([](Json& j) { j["floors"][0]["decks"][0].erase("outline"); }, "/floors/0/decks/0/outline");
    expectBad([](Json& j) { j["floors"][0]["decks"][0]["outline"][0]["z"] = 1; }, "/floors/0/decks/0/outline/0/z");
    expectBad([](Json& j) { j["floors"][0]["decks"][0]["outline"][0].erase("y"); }, "/floors/0/decks/0/outline/0/y");
    expectBad([](Json& j) { j["floors"][0]["walls"][0]["cladding"][0].erase("material"); }, "/floors/0/walls/0/cladding/0/material");
    expectBad([](Json& j) { j["floors"][0]["walls"][0]["cladding"][0]["side"] = "up"; }, "/floors/0/walls/0/cladding/0/side");
    expectBad([](Json& j) { j["floors"][0]["light_strips"][0]["kind"] = "spot"; }, "/floors/0/light_strips/0/kind");
    expectBad([](Json& j) { j["floors"][0]["light_strips"][0].erase("kind"); }, "/floors/0/light_strips/0/kind");
    expectBad([](Json& j) { j["floors"][0]["stairs"][0]["cheeks"] = "yes"; }, "/floors/0/stairs/0/cheeks");
    expectBad([](Json& j) { j["floors"][0]["railings"][0].erase("points"); }, "/floors/0/railings/0/points");
    expectBad([](Json& j) { j["floors"][1]["dormers"][0].erase("x"); }, "/floors/1/dormers/0/x");
    expectBad([](Json& j) { j["roof"]["rise"] = "high"; }, "/roof/rise");
    expectBad([](Json& j) { j["site"]["boundaries"][0]["kind"] = "hedge"; }, "/site/boundaries/0/kind");
    expectBad([](Json& j) { j["site"]["boundaries"][0]["gates"][0].erase("offset"); }, "/site/boundaries/0/gates/0/offset");
    expectBad([](Json& j) { j["site"]["surfaces"][0]["kind"] = "pond"; }, "/site/surfaces/0/kind");
    expectBad([](Json& j) { j["site"]["ground"]["colour"] = 1; }, "/site/ground/colour");
    expectBad([](Json& j) { j["site"]["extra"] = 1; }, "/site/extra");
    expectBad([](Json& j) { j["foundation"]["step_out"] = nullptr; }, "/foundation/step_out");
    expectBad([](Json& j) { j["ground_floor"] = 1; }, "/ground_floor");
    // A rule of the plan check comes back as BadFile at the same place.
    expectBad([](Json& j) { j["foundation"]["height"] = 5.0; }, "/foundation/height");
}

TEST_CASE("checkPlan refuses every broken format 8 rule", "[core][plan][format8][check]")
{
    SECTION("cladding") {
        const std::string at = "/floors/0/walls/0/cladding/0";
        const auto zone = [](Plan& p) -> CladdingZone& { return p.floors[0].walls[0].cladding[0]; };
        refused([&](Plan& p) { zone(p).id = "w1"; }, ErrorCode::AlreadyExists, at + "/id");
        refused([&](Plan& p) { zone(p).start = -0.1; }, ErrorCode::InvalidValue, at + "/start");
        refused([&](Plan& p) { zone(p).end = 0.5; }, ErrorCode::InvalidValue, at + "/end");
        refused([&](Plan& p) { zone(p).end = kNaN; }, ErrorCode::InvalidValue, at + "/end");
        refused([&](Plan& p) { zone(p).bottom = -0.1; }, ErrorCode::InvalidValue, at + "/bottom");
        refused([&](Plan& p) { zone(p).top = 0.1; }, ErrorCode::InvalidValue, at + "/top");
        refused([&](Plan& p) { zone(p).depth = 0.004; }, ErrorCode::InvalidValue, at + "/depth");
        refused([&](Plan& p) { zone(p).depth = 0.21; }, ErrorCode::InvalidValue, at + "/depth");
        refused([&](Plan& p) { zone(p).material = ""; }, ErrorCode::InvalidValue, at + "/material");
        refused([&](Plan& p) { zone(p).material = "bad name"; }, ErrorCode::InvalidValue, at + "/material");
        accepted([&](Plan& p) { zone(p).depth = 0.2; });
    }
    SECTION("opening fill") {
        refused([](Plan& p) { p.floors[0].openings[1].fill = "bad fill"; }, ErrorCode::InvalidValue, "/floors/0/openings/1/fill");
        accepted([](Plan& p) { p.floors[0].openings[1].fill = "not_registered_here"; });
    }
    SECTION("items standing on items") {
        refused([](Plan& p) { p.floors[0].items[0].on = "i1"; }, ErrorCode::InvalidValue, "/floors/0/items/0/on");
        refused([](Plan& p) { p.floors[0].items[0].on = "i9"; }, ErrorCode::NotFound, "/floors/0/items/0/on");
        refused([](Plan& p) { p.floors[0].items[0].on = "w1"; }, ErrorCode::NotFound, "/floors/0/items/0/on");
        refused([](Plan& p) { p.floors[0].items[1].on = "i3"; }, ErrorCode::InvalidValue, "/floors/0/items/1/on");
        refused([](Plan& p) { p.floors[0].items[2].on = "i1"; }, ErrorCode::InvalidValue, "/floors/0/items/0/on");
        refused(
            [](Plan& p) {
                p.floors[0].items[1].wall.reset();
                p.floors[0].items[1].on = "i1";
                p.floors[0].items[2].on = "i2";
            },
            ErrorCode::InvalidValue, "/floors/0/items/0/on");
        accepted([](Plan& p) {
            p.floors[0].items[1].wall.reset();
            p.floors[0].items[1].on = "i1";
        });
    }
    SECTION("outside steps") {
        refused([](Plan& p) { p.floors[0].stairs[0].rise = 0.05; }, ErrorCode::InvalidValue, "/floors/0/stairs/0/rise");
        refused([](Plan& p) { p.floors[0].stairs[0].rise = 6.5; }, ErrorCode::InvalidValue, "/floors/0/stairs/0/rise");
        refused([](Plan& p) { p.floors[0].stairs[0].rise = kNaN; }, ErrorCode::InvalidValue, "/floors/0/stairs/0/rise");
        refused([](Plan& p) { p.floors[0].stairs[0].width = 6.1; }, ErrorCode::InvalidValue, "/floors/0/stairs/0/width");
        accepted([](Plan& p) { p.floors[0].stairs[0].width = 6.0; });
        accepted([](Plan& p) { p.floors[0].stairs[0].rise = 6.0; });
    }
    SECTION("roof settings") {
        for (const std::string at : {std::string("/roof"), std::string("/roof_sections/0/settings"), std::string("/floors/0/canopies/0/roof")}) {
            INFO(at);
            const auto settings = [at](Plan& p) -> RoofSettings& {
                if (at == "/roof") return p.roof;
                if (at == "/roof_sections/0/settings") return p.roofSections[0].settings;
                return p.floors[0].canopies[0].roof;
            };
            refused([&](Plan& p) { settings(p).secondPitch = -1.0; }, ErrorCode::InvalidValue, at + "/second_pitch");
            refused([&](Plan& p) { settings(p).secondPitch = 76.0; }, ErrorCode::InvalidValue, at + "/second_pitch");
            refused([&](Plan& p) { settings(p).breakRatio = 0.05; }, ErrorCode::InvalidValue, at + "/break_ratio");
            refused([&](Plan& p) { settings(p).breakRatio = 0.95; }, ErrorCode::InvalidValue, at + "/break_ratio");
            refused([&](Plan& p) { settings(p).ridgeOffset = -0.5; }, ErrorCode::InvalidValue, at + "/ridge_offset");
            refused([&](Plan& p) { settings(p).ridgeOffset = 0.46; }, ErrorCode::InvalidValue, at + "/ridge_offset");
            refused([&](Plan& p) { settings(p).rise = 0.05; }, ErrorCode::InvalidValue, at + "/rise");
            refused([&](Plan& p) { settings(p).rise = 10.5; }, ErrorCode::InvalidValue, at + "/rise");
            refused([&](Plan& p) { settings(p).rise = kNaN; }, ErrorCode::InvalidValue, at + "/rise");
            accepted([&](Plan& p) {
                settings(p).breakRatio = 0.9;
                settings(p).ridgeOffset = -0.45;
                settings(p).rise = 10.0;
                settings(p).secondPitch = 0.0;
            });
        }
    }
    SECTION("decks") {
        const std::string at = "/floors/0/decks/0";
        refused([](Plan& p) { p.floors[0].decks[0].id = "o1"; }, ErrorCode::AlreadyExists, at + "/id");
        refused([](Plan& p) { p.floors[0].decks[0].outline.resize(2); }, ErrorCode::InvalidValue, at + "/outline");
        refused([](Plan& p) { p.floors[0].decks[0].outline = {{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}}; }, ErrorCode::InvalidValue,
                at + "/outline");
        refused([](Plan& p) { p.floors[0].decks[0].outline = {{0.0, 0.0}, {0.09, 0.0}, {0.09, 0.09}, {0.0, 0.09}}; }, ErrorCode::InvalidValue,
                at + "/outline");
        refused([](Plan& p) { p.floors[0].decks[0].outline[1].x = kNaN; }, ErrorCode::InvalidValue, at + "/outline/1/x");
        refused([](Plan& p) { p.floors[0].decks[0].top = -0.1; }, ErrorCode::InvalidValue, at + "/top");
        refused([](Plan& p) { p.floors[0].decks[0].top = 10.5; }, ErrorCode::InvalidValue, at + "/top");
        refused([](Plan& p) { p.floors[0].decks[0].thickness = 0.04; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.floors[0].decks[0].thickness = 1.1; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.floors[0].decks[0].material = "bad name"; }, ErrorCode::InvalidValue, at + "/material");
        refused([](Plan& p) { p.floors[0].decks[0].skirtMaterial = "bad name"; }, ErrorCode::InvalidValue, at + "/skirt_material");
        accepted([](Plan& p) { p.floors[0].decks[0].outline = {{0.0, 0.0}, {0.2, 0.0}, {0.2, 0.2}}; });
    }
    SECTION("railings") {
        const std::string at = "/floors/0/railings/0";
        refused([](Plan& p) { p.floors[0].railings[0].id = "dk1"; }, ErrorCode::AlreadyExists, at + "/id");
        refused([](Plan& p) { p.floors[0].railings[0].points.resize(1); }, ErrorCode::InvalidValue, at + "/points");
        refused([](Plan& p) { p.floors[0].railings[0].points[0].y = kNaN; }, ErrorCode::InvalidValue, at + "/points/0/y");
        refused([](Plan& p) { p.floors[0].railings[0].host = "dk9"; }, ErrorCode::NotFound, at + "/host");
        refused([](Plan& p) { p.floors[0].railings[0].host = "i1"; }, ErrorCode::NotFound, at + "/host");
        refused([](Plan& p) { p.floors[0].railings[0].base = -0.1; }, ErrorCode::InvalidValue, at + "/base");
        refused([](Plan& p) { p.floors[0].railings[0].height = 0.2; }, ErrorCode::InvalidValue, at + "/height");
        refused([](Plan& p) { p.floors[0].railings[0].height = 3.1; }, ErrorCode::InvalidValue, at + "/height");
        refused([](Plan& p) { p.floors[0].railings[0].style = "bad style"; }, ErrorCode::InvalidValue, at + "/style");
        refused([](Plan& p) { p.floors[0].railings[0].style.clear(); }, ErrorCode::InvalidValue, at + "/style");
        refused([](Plan& p) { p.floors[0].railings[0].material = "bad name"; }, ErrorCode::InvalidValue, at + "/material");
        refused([](Plan& p) { p.floors[0].railings[0].glassMaterial = "bad name"; }, ErrorCode::InvalidValue, at + "/glass_material");
        accepted([](Plan& p) { p.floors[0].railings[0].host.reset(); });
    }
    SECTION("canopies") {
        const std::string at = "/floors/0/canopies/0";
        refused([](Plan& p) { p.floors[0].canopies[0].id = "s1"; }, ErrorCode::AlreadyExists, at + "/id");
        refused([](Plan& p) { p.floors[0].canopies[0].outline.clear(); }, ErrorCode::InvalidValue, at + "/outline");
        refused([](Plan& p) { p.floors[0].canopies[0].height = 0.9; }, ErrorCode::InvalidValue, at + "/height");
        refused([](Plan& p) { p.floors[0].canopies[0].height = 21.0; }, ErrorCode::InvalidValue, at + "/height");
        refused([](Plan& p) { p.floors[0].canopies[0].thickness = 0.01; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.floors[0].canopies[0].thickness = 1.5; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.floors[0].canopies[0].roof.type = "bad type"; }, ErrorCode::InvalidValue, at + "/roof/type");
        refused([](Plan& p) { p.floors[0].canopies[0].roof.pitch = 80.0; }, ErrorCode::InvalidValue, at + "/roof/pitch");
        refused([](Plan& p) { p.floors[0].canopies[0].material = "bad name"; }, ErrorCode::InvalidValue, at + "/material");
        refused([](Plan& p) { p.floors[0].canopies[0].undersideMaterial = "bad name"; }, ErrorCode::InvalidValue, at + "/underside_material");
        refused([](Plan& p) { p.floors[0].canopies[0].fasciaMaterial = "bad name"; }, ErrorCode::InvalidValue, at + "/fascia_material");
    }
    SECTION("dormers") {
        const std::string at = "/floors/1/dormers/0";
        const auto dormer = [](Plan& p) -> Dormer& { return p.floors[1].dormers[0]; };
        refused([&](Plan& p) { dormer(p).id = "f1"; }, ErrorCode::AlreadyExists, at + "/id");
        refused([&](Plan& p) { dormer(p).x = kNaN; }, ErrorCode::InvalidValue, at + "/x");
        refused([&](Plan& p) { dormer(p).y = kNaN; }, ErrorCode::InvalidValue, at + "/y");
        refused([&](Plan& p) { dormer(p).type = "bad type"; }, ErrorCode::InvalidValue, at + "/type");
        refused([&](Plan& p) { dormer(p).width = 0.4; }, ErrorCode::InvalidValue, at + "/width");
        refused([&](Plan& p) { dormer(p).width = 6.5; }, ErrorCode::InvalidValue, at + "/width");
        refused([&](Plan& p) { dormer(p).depth = 0.4; }, ErrorCode::InvalidValue, at + "/depth");
        refused([&](Plan& p) { dormer(p).depth = 6.5; }, ErrorCode::InvalidValue, at + "/depth");
        refused([&](Plan& p) { dormer(p).pitch = 4.0; }, ErrorCode::InvalidValue, at + "/pitch");
        refused([&](Plan& p) { dormer(p).pitch = 76.0; }, ErrorCode::InvalidValue, at + "/pitch");
        refused([&](Plan& p) { dormer(p).windowWidth = 0.1; }, ErrorCode::InvalidValue, at + "/window_width");
        refused([&](Plan& p) { dormer(p).windowWidth = 2.1; }, ErrorCode::InvalidValue, at + "/window_width");
        refused([&](Plan& p) { dormer(p).windowHeight = 0.1; }, ErrorCode::InvalidValue, at + "/window_height");
        refused([&](Plan& p) { dormer(p).windowSill = -0.1; }, ErrorCode::InvalidValue, at + "/window_sill");
        refused([&](Plan& p) { dormer(p).material = "bad name"; }, ErrorCode::InvalidValue, at + "/material");
        refused([&](Plan& p) { dormer(p).roofMaterial = "bad name"; }, ErrorCode::InvalidValue, at + "/roof_material");
        accepted([&](Plan& p) { dormer(p).windowWidth = 2.0; });
    }
    SECTION("light strips") {
        refused([](Plan& p) { p.floors[0].lightStrips[0].id = "l1x"; p.floors[0].lightStrips[1].id = "l1x"; }, ErrorCode::AlreadyExists,
                "/floors/0/light_strips/1/id");
        refused([](Plan& p) { p.floors[0].lightStrips[0].points.resize(1); }, ErrorCode::InvalidValue, "/floors/0/light_strips/0/points");
        refused([](Plan& p) { p.floors[0].lightStrips[1].stair.reset(); }, ErrorCode::InvalidValue, "/floors/0/light_strips/1/stair");
        refused([](Plan& p) { p.floors[0].lightStrips[1].stair = "s9"; }, ErrorCode::NotFound, "/floors/0/light_strips/1/stair");
        refused([](Plan& p) { p.floors[0].lightStrips[0].z = -0.1; }, ErrorCode::InvalidValue, "/floors/0/light_strips/0/z");
        refused([](Plan& p) { p.floors[0].lightStrips[0].z = 2.8; }, ErrorCode::InvalidValue, "/floors/0/light_strips/0/z");
        refused([](Plan& p) { p.floors[0].lightStrips[0].power = -1.0; }, ErrorCode::InvalidValue, "/floors/0/light_strips/0/power");
        refused([](Plan& p) { p.floors[0].lightStrips[0].power = 201.0; }, ErrorCode::InvalidValue, "/floors/0/light_strips/0/power");
        accepted([](Plan& p) { p.floors[0].lightStrips[0].z = 2.7; });
    }
    SECTION("ground floor") {
        refused([](Plan& p) { p.groundFloor = "f9"; }, ErrorCode::NotFound, "/ground_floor");
        refused([](Plan& p) { p.groundFloor = "w1"; }, ErrorCode::NotFound, "/ground_floor");
        accepted([](Plan& p) { p.groundFloor = "f2"; });
        accepted([](Plan& p) { p.groundFloor.reset(); });
    }
    SECTION("site ground, boundaries and gates") {
        const std::string at = "/site/boundaries/0";
        refused([](Plan& p) { p.site.ground.margin = -1.0; }, ErrorCode::InvalidValue, "/site/ground/margin");
        refused([](Plan& p) { p.site.ground.margin = 501.0; }, ErrorCode::InvalidValue, "/site/ground/margin");
        refused([](Plan& p) { p.site.ground.material = "bad name"; }, ErrorCode::InvalidValue, "/site/ground/material");
        refused([](Plan& p) { p.site.boundaries[0].id = "w1"; }, ErrorCode::AlreadyExists, at + "/id");
        refused([](Plan& p) { p.site.boundaries[0].points.resize(1); }, ErrorCode::InvalidValue, at + "/points");
        refused([](Plan& p) { p.site.boundaries[0].height = 0.2; }, ErrorCode::InvalidValue, at + "/height");
        refused([](Plan& p) { p.site.boundaries[0].height = 4.1; }, ErrorCode::InvalidValue, at + "/height");
        refused([](Plan& p) { p.site.boundaries[0].thickness = 0.01; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.site.boundaries[0].thickness = 1.1; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.site.boundaries[0].style = "bad style"; }, ErrorCode::InvalidValue, at + "/style");
        refused([](Plan& p) { p.site.boundaries[0].material = "bad name"; }, ErrorCode::InvalidValue, at + "/material");
        refused([](Plan& p) { p.site.boundaries[0].gates[0].id = "sb1"; }, ErrorCode::AlreadyExists, at + "/gates/0/id");
        refused([](Plan& p) { p.site.boundaries[0].gates[0].offset = -0.5; }, ErrorCode::InvalidValue, at + "/gates/0/offset");
        refused([](Plan& p) { p.site.boundaries[0].gates[0].offset = 44.5; }, ErrorCode::InvalidValue, at + "/gates/0/offset");
        refused(
            [](Plan& p) {
                p.site.boundaries[0].closed = false;
                p.site.boundaries[0].gates[0].offset = 40.0;
            },
            ErrorCode::InvalidValue, at + "/gates/0/offset");
        refused([](Plan& p) { p.site.boundaries[0].gates[0].width = 0.4; }, ErrorCode::InvalidValue, at + "/gates/0/width");
        refused([](Plan& p) { p.site.boundaries[0].gates[0].width = 6.5; }, ErrorCode::InvalidValue, at + "/gates/0/width");
        refused([](Plan& p) { p.site.boundaries[0].gates[0].material = "bad name"; }, ErrorCode::InvalidValue, at + "/gates/0/material");
        accepted([](Plan& p) { p.site.boundaries[0].gates[0].offset = 44.0; });
    }
    SECTION("site surfaces") {
        const std::string at = "/site/surfaces/1";
        refused([](Plan& p) { p.site.surfaces[1].id = "g1"; }, ErrorCode::AlreadyExists, at + "/id");
        refused([](Plan& p) { p.site.surfaces[1].outline.pop_back(); }, ErrorCode::InvalidValue, at + "/outline");
        refused([](Plan& p) { p.site.surfaces[1].thickness = -0.01; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.site.surfaces[1].thickness = 0.6; }, ErrorCode::InvalidValue, at + "/thickness");
        refused([](Plan& p) { p.site.surfaces[1].material = "bad name"; }, ErrorCode::InvalidValue, at + "/material");
    }
    SECTION("foundation") {
        refused([](Plan& p) { p.foundation.height = 0.04; }, ErrorCode::InvalidValue, "/foundation/height");
        refused([](Plan& p) { p.foundation.height = 2.1; }, ErrorCode::InvalidValue, "/foundation/height");
        refused([](Plan& p) { p.foundation.stepOut = -0.01; }, ErrorCode::InvalidValue, "/foundation/step_out");
        refused([](Plan& p) { p.foundation.stepOut = 0.31; }, ErrorCode::InvalidValue, "/foundation/step_out");
        refused([](Plan& p) { p.foundation.material = "bad name"; }, ErrorCode::InvalidValue, "/foundation/material");
        accepted([](Plan& p) { p.foundation.enabled = false; p.foundation.stepOut = 0.0; });
    }
}

TEST_CASE("format 8 objects are found by id and count as used ids", "[core][plan][format8][id]")
{
    Plan plan = everyFieldPlan();
    Floor& f1 = plan.floors[0];
    CHECK(findDeck(f1, "dk1") == &f1.decks[0]);
    CHECK(findDeck(f1, "rl1") == nullptr);
    CHECK(findRailing(f1, "rl1") == &f1.railings[0]);
    CHECK(findCanopy(f1, "cn1") == &f1.canopies[0]);
    CHECK(findDormer(plan.floors[1], "dm1") == &plan.floors[1].dormers[0]);
    CHECK(findDormer(f1, "dm1") == nullptr);
    CHECK(findLightStrip(f1, "ls2") == &f1.lightStrips[1]);

    Wall* holder = nullptr;
    CHECK(findCladding(f1, "cz1", &holder) == &f1.walls[0].cladding[0]);
    CHECK(holder == &f1.walls[0]);
    CHECK(findCladding(f1, "cz9", &holder) == nullptr);
    CHECK(holder == nullptr);
    const Floor& constFloor = f1;
    const Wall* constHolder = nullptr;
    CHECK(findCladding(constFloor, "cz1", &constHolder) != nullptr);
    CHECK(constHolder == &f1.walls[0]);

    CHECK(findBoundary(plan, "sb2") == &plan.site.boundaries[1]);
    SiteBoundary* owner = nullptr;
    CHECK(findGate(plan, "g1", &owner) == &plan.site.boundaries[0].gates[0]);
    CHECK(owner == &plan.site.boundaries[0]);
    CHECK(findGate(plan, "sb1", &owner) == nullptr);
    CHECK(owner == nullptr);
    const Plan& constPlan = plan;
    CHECK(findSiteSurface(constPlan, "ss2") == &plan.site.surfaces[1]);
    CHECK(findSiteSurface(constPlan, "ss9") == nullptr);

    for (const char* id : {"cz1", "dk1", "rl1", "cn1", "ls1"}) {
        INFO(id);
        CHECK(findFloorContaining(plan, id) == &plan.floors[0]);
        CHECK(idInUse(plan, id));
    }
    CHECK(findFloorContaining(plan, "dm1") == &plan.floors[1]);
    for (const char* id : {"sb1", "sb2", "g1", "ss1", "ss2", "dm1"}) {
        INFO(id);
        CHECK(idInUse(plan, id));
    }
    CHECK(findFloorContaining(plan, "sb1") == nullptr);
    CHECK_FALSE(idInUse(plan, "dk2"));

    for (const std::string_view prefix : {kCladdingIdPrefix, kDeckIdPrefix, kRailingIdPrefix, kCanopyIdPrefix, kDormerIdPrefix,
                                          kLightStripIdPrefix, kBoundaryIdPrefix, kGateIdPrefix, kSiteSurfaceIdPrefix}) {
        const Id fresh = newId(plan, prefix);
        INFO(fresh);
        CHECK(fresh.rfind(std::string(prefix), 0) == 0);
        CHECK(isValidId(fresh));
        CHECK_FALSE(idInUse(plan, fresh));
    }
}

TEST_CASE("the ground level floor is the one named, else the lowest", "[core][plan][format8]")
{
    Plan plan = test::fullPlan();
    CHECK(&groundLevelFloor(plan) == &plan.floors[0]);
    plan.groundFloor = "f2";
    CHECK(&groundLevelFloor(plan) == &plan.floors[1]);
    plan.groundFloor = "f9";
    CHECK(&groundLevelFloor(plan) == &plan.floors[0]);
}

TEST_CASE("the format 8 kind names read back", "[core][plan][format8]")
{
    for (const BoundaryKind kind : {BoundaryKind::Fence, BoundaryKind::Wall}) {
        CHECK(boundaryKindFromName(boundaryKindName(kind)) == kind);
    }
    CHECK(boundaryKindName(BoundaryKind::Fence) == "fence");
    CHECK(boundaryKindName(BoundaryKind::Wall) == "wall");
    CHECK_FALSE(boundaryKindFromName("hedge").has_value());

    for (const SiteSurfaceKind kind : {SiteSurfaceKind::Path, SiteSurfaceKind::Driveway, SiteSurfaceKind::Paving, SiteSurfaceKind::Lawn}) {
        CHECK(siteSurfaceKindFromName(siteSurfaceKindName(kind)) == kind);
    }
    CHECK(siteSurfaceKindName(SiteSurfaceKind::Driveway) == "driveway");
    CHECK(siteSurfaceKindName(SiteSurfaceKind::Lawn) == "lawn");
    CHECK_FALSE(siteSurfaceKindFromName("Path").has_value());

    for (const LightStripKind kind : {LightStripKind::Cove, LightStripKind::Soffit, LightStripKind::Edge, LightStripKind::Step}) {
        CHECK(lightStripKindFromName(lightStripKindName(kind)) == kind);
    }
    CHECK(lightStripKindName(LightStripKind::Soffit) == "soffit");
    CHECK(lightStripKindName(LightStripKind::Step) == "step");
    CHECK_FALSE(lightStripKindFromName("spot").has_value());
}

TEST_CASE("checkRoofSettings checks the format 8 roof fields", "[core][roofs][format8]")
{
    RoofSettings s;
    s.type = "gable";
    CHECK(checkRoofSettings(s).ok());
    const auto refusedWith = [](RoofSettings changed) { return checkRoofSettings(changed).code == ErrorCode::InvalidValue; };
    RoofSettings bad = s;
    bad.secondPitch = 80.0;
    CHECK(refusedWith(bad));
    bad = s;
    bad.secondPitch = -0.1;
    CHECK(refusedWith(bad));
    bad = s;
    bad.breakRatio = 0.0;
    CHECK(refusedWith(bad));
    bad = s;
    bad.breakRatio = 1.0;
    CHECK(refusedWith(bad));
    bad = s;
    bad.ridgeOffset = 0.5;
    CHECK(refusedWith(bad));
    bad = s;
    bad.ridgeOffset = kNaN;
    CHECK(refusedWith(bad));
    bad = s;
    bad.rise = 0.0;
    CHECK(refusedWith(bad));
    bad = s;
    bad.rise = 11.0;
    CHECK(refusedWith(bad));
    RoofSettings edge = s;
    edge.secondPitch = 75.0;
    edge.breakRatio = 0.1;
    edge.ridgeOffset = 0.45;
    edge.rise = 0.1;
    CHECK(checkRoofSettings(edge).ok());
}

TEST_CASE("stairs may be up to 6 metres wide", "[core][stairs][format8]")
{
    Stair s;
    s.id = "s1";
    s.width = 6.0;
    CHECK(kMaxStairWidth == 6.0);
    CHECK(checkStair(s).ok());
    s.width = 6.01;
    CHECK(checkStair(s).code == ErrorCode::InvalidValue);
    CHECK(checkStair(s).message.find("6 metres") != std::string::npos);
}

TEST_CASE("project format 7 writes the new default materials", "[core][json][format8][materials]")
{
    const ProjectSettings defaults{};
    const Json j = settingsToJson(defaults);
    CHECK(j["version"] == 7);
    CHECK(j["materials"]["deck"] == "oak_planks");
    CHECK(j["materials"]["foundation"] == "concrete");
    CHECK(j["materials"]["ground"] == "grass");
    CHECK(j["materials"]["paving"] == "concrete");
    CHECK(j["materials"]["fence"] == "painted_wood");
    CHECK(j["materials"]["boundary_wall"] == "red_brick");
    CHECK(schemaProblem("project.schema.json", plain(j)) == "");
    CHECK(schemaProblem("project.v6.schema.json", plain(j)) != "");

    ProjectSettings changed;
    setMaterialDefault(changed.materials, Surface::Ground, "lawn_green");
    setMaterialDefault(changed.materials, Surface::Fence, std::nullopt);
    const SettingsRead read = settingsFromJson(Json::parse(settingsToJson(changed).dump()));
    test::requireOk(read.status);
    CHECK(read.settings == changed);
    CHECK_FALSE(read.migrated);

    Json bad = settingsToJson(defaults);
    bad["materials"]["paving"] = "bad name";
    const SettingsRead refusedRead = settingsFromJson(bad);
    CHECK(refusedRead.status.code == ErrorCode::BadFile);
    CHECK(refusedRead.status.where == "/materials/paving");
}

TEST_CASE("a version 6 project reads, upgrades and writes as version 7", "[core][json][format8][migrate]")
{
    const Json file = dataFile("v6-project.json");
    REQUIRE(file["version"] == 6);
    CHECK(schemaProblem("project.v6.schema.json", plain(file)) == "");
    CHECK(schemaProblem("project.schema.json", plain(file)) != "");
    const SettingsRead read = settingsFromJson(file);
    test::requireOk(read.status);
    CHECK(read.fileVersion == 6);
    CHECK(read.migrated);
    CHECK(read.settings.materials.wallInside == "plaster_white");
    CHECK(read.settings.materials.deck == "oak_planks");
    CHECK(read.settings.materials.ground == "grass");
    CHECK(read.settings.materials.boundaryWall == "red_brick");
    const Json written = settingsToJson(read.settings);
    CHECK(written["version"] == 7);
    CHECK(schemaProblem("project.schema.json", plain(written)) == "");

    Json older = settingsToJson(ProjectSettings{});
    test::projectBackTo6(older);
    CHECK(schemaProblem("project.v6.schema.json", plain(older)) == "");
    const SettingsRead upgraded = settingsFromJson(older);
    test::requireOk(upgraded.status);
    CHECK(upgraded.settings == ProjectSettings{});

    Json kept = settingsToJson(ProjectSettings{});
    kept["version"] = 6;
    kept["materials"]["deck"] = "teak";
    const SettingsRead keptRead = settingsFromJson(kept);
    test::requireOk(keptRead.status);
    CHECK(keptRead.settings.materials.deck == "teak");
}

TEST_CASE("the new surfaces have names and no build uses them yet", "[core][materials][format8]")
{
    CHECK(surfaceName(Surface::Deck) == "deck");
    CHECK(surfaceName(Surface::Foundation) == "foundation");
    CHECK(surfaceName(Surface::Ground) == "ground");
    CHECK(surfaceName(Surface::Paving) == "paving");
    CHECK(surfaceName(Surface::Fence) == "fence");
    CHECK(surfaceName(Surface::BoundaryWall) == "boundary_wall");
    CHECK(surfaceFromName("boundary_wall") == Surface::BoundaryWall);
    const MaterialDefaults d;
    CHECK(materialDefault(d, Surface::Ground) == std::optional<std::string>(std::string(kSampleGrass)));
    CHECK(materialDefault(d, Surface::Foundation) == std::optional<std::string>("concrete"));

    const Project project = makeNewProject();
    const std::vector<std::string> used = materialsUsed(project);
    CHECK(std::find(used.begin(), used.end(), std::string(kSampleGrass)) == used.end());
}

TEST_CASE("the engine and checksum error codes have names and descriptions", "[core][status][format8]")
{
    CHECK(errorCodeName(ErrorCode::EngineNotFound) == "engine_not_found");
    CHECK(errorCodeName(ErrorCode::EngineFailed) == "engine_failed");
    CHECK(errorCodeName(ErrorCode::ChecksumFailed) == "checksum_failed");
    const auto codes = allErrorCodes();
    for (const ErrorCode code : {ErrorCode::EngineNotFound, ErrorCode::EngineFailed, ErrorCode::ChecksumFailed}) {
        INFO(errorCodeName(code));
        CHECK_FALSE(errorCodeDescription(code).empty());
        CHECK(std::count(codes.begin(), codes.end(), code) == 1);
    }
    CHECK(codes.back() == ErrorCode::ChecksumFailed);
}
