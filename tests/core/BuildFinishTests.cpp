#include "BuildFixtures.h"
#include "SchemaSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Kit.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

// The square room of the build fixtures with only its door, no window and no items.
Project finishProject()
{
    Project p = test::buildProject();
    Floor& f = p.plan.floors[0];
    f.openings.resize(1);
    f.items.clear();
    f.rooms.resize(1);
    return p;
}

// A source with the sofa, a door, a wide door, a shelf and books, each with its size, and the lamp broken.
BuildAssetSource sizedSource()
{
    return [](std::string_view id) -> std::optional<BuildAssetState> {
        const std::string path = "D:/Library/assets/items/" + std::string(id) + "/asset.blend";
        if (id == "lamp") return BuildAssetState{path, true, "asset.blend is missing.", {}, std::nullopt};
        if (id == "door_a") return BuildAssetState{path, false, "", {}, ItemSize{0.9, 0.05, 2.1}};
        if (id == "door_wide") return BuildAssetState{path, false, "", {}, ItemSize{1.2, 0.05, 2.1}};
        if (id == "shelf") return BuildAssetState{path, false, "", {}, ItemSize{0.8, 0.3, 1.8}};
        if (id == "books") return BuildAssetState{path, false, "", {}, ItemSize{0.1, 0.2, 0.25}};
        if (id == "sofa") return BuildAssetState{path, false, "", {}, std::nullopt};
        return std::nullopt;
    };
}

// The job of a build plan, checked against the job schema.
Json checkedJob(const Project& project, const BuildPlan& plan)
{
    const Json job = makeBuildJob(project, plan, BuildSettings{}, "D:/Projects/Flat", "job-1", "D:/Projects/Flat/build/result.json");
    INFO(job.dump().substr(0, 200));
    CHECK(test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump())) == "");
    return job;
}

// The problems of a plan with a code.
std::vector<BuildProblem> problemsWith(const BuildPlan& plan, const std::string& code)
{
    std::vector<BuildProblem> out;
    for (const BuildProblem& p : plan.problems) {
        if (p.code == code) out.push_back(p);
    }
    return out;
}

// Says whether the slot table has a slot.
bool hasSlot(const BuildPlan& plan, const std::string& name, const std::string& material, const std::string& surface)
{
    return std::any_of(plan.materialSlots.begin(), plan.materialSlots.end(), [&](const BuildSlot& s) {
        return s.name == name && s.material == material && s.surface == surface;
    });
}

}

TEST_CASE("the build plan carries the trim of every floor with its slot", "[buildjob][finish]")
{
    const Project p = finishProject();
    const BuildPlan plan = makeBuildPlan(p, test::sampleAssetSource(), test::sampleMaterialSource());
    REQUIRE(plan.finish.size() == 1);
    const FloorFinish& finish = plan.finish[0];
    CHECK(finish.floor == "f1");
    REQUIRE(finish.trim.size() == 3);
    CHECK(finish.trim[0].kind == TrimKind::Skirting);
    CHECK(finish.trim[1].kind == TrimKind::Cornice);
    CHECK(finish.trim[2].kind == TrimKind::Casing);
    CHECK(finish.trimSlot == "Trim");
    // The build fixtures clear every default material, so the trim gets its neutral look.
    CHECK(hasSlot(plan, "Trim", "", "Trim"));
    CHECK(problemsWith(plan, "trim_profile_missing").empty());

    const Json job = checkedJob(p, plan);
    const Json& floor = job["params"]["floors"][0];
    REQUIRE(floor["trim"].size() == 3);
    CHECK(floor["trim"][0]["object"] == "SM_f1_Skirting");
    CHECK(floor["trim"][0]["plynth_kind"] == "trim");
    CHECK(floor["trim"][2]["kind"] == "casing");
    CHECK(floor["trim"][0]["slot"] == "Trim");
    CHECK_FALSE(floor.contains("lights"));
    CHECK_FALSE(floor.contains("dressing"));
    CHECK_FALSE(floor["openings"][0].contains("asset"));

    Project off = p;
    for (TrimKind kind : {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill}) {
        trimStyle(off.settings.trim, kind).enabled = false;
    }
    const BuildPlan bare = makeBuildPlan(off, test::sampleAssetSource());
    CHECK(bare.finish[0].trim.empty());
    CHECK_FALSE(checkedJob(off, bare)["params"]["floors"][0].contains("trim"));

    Project missing = p;
    missing.settings.trim.cornice.profile = "not_installed";
    const BuildPlan warned = makeBuildPlan(missing, test::sampleAssetSource());
    REQUIRE(problemsWith(warned, "trim_profile_missing").size() == 1);
    CHECK(problemsWith(warned, "trim_profile_missing")[0].severity == BuildSeverity::Warning);
}

TEST_CASE("lights stand at their floor's level with their power", "[buildjob][finish]")
{
    Project p = finishProject();
    Floor upper;
    upper.id = "f2";
    upper.name = "Upstairs";
    LightMarker lamp;
    lamp.id = "l2";
    lamp.kind = LightKind::Standing;
    lamp.x = 1.0;
    lamp.y = 2.0;
    lamp.z = 1.4;
    upper.lights = {lamp};
    p.plan.floors.push_back(upper);
    LightMarker ceiling;
    ceiling.id = "l1";
    ceiling.room = "r1";
    ceiling.x = 2.0;
    ceiling.y = 2.0;
    ceiling.z = 2.9;
    p.plan.floors[0].lights = {ceiling};
    test::requirePlanValid(p.plan);

    const BuildPlan plan = makeBuildPlan(p, test::sampleAssetSource());
    REQUIRE(plan.finish.size() == 2);
    REQUIRE(plan.finish[0].lights.size() == 1);
    CHECK(plan.finish[0].lights[0].power == kCeilingLightPower);
    CHECK_THAT(plan.finish[0].lights[0].location.z, WithinAbs(2.9, 1e-12));
    CHECK_THAT(plan.finish[0].lights[0].location.y, WithinAbs(-2.0, 1e-12));
    REQUIRE(plan.finish[1].lights.size() == 1);
    CHECK(plan.finish[1].lights[0].power == kStandingLightPower);
    CHECK_THAT(plan.finish[1].lights[0].location.z, WithinAbs(3.2 + 1.4, 1e-12));

    const Json job = checkedJob(p, plan);
    const Json& light = job["params"]["floors"][1]["lights"][0];
    CHECK(light["id"] == "l2");
    CHECK(light["kind"] == "standing");
    CHECK(light["power"] == 30.0);
    CHECK(light["color"].size() == 3);
}

TEST_CASE("a named door asset is fitted, listed once, and refused when missing or broken", "[buildjob][finish]")
{
    Project p = finishProject();
    p.plan.floors[0].openings[0].frame = "door_a";
    Item shelf = test::buildItem("i1", "door_a", 1.0, 1.0, {0.9, 0.05, 2.1});
    p.plan.floors[0].items = {shelf};
    BuildPlan plan = makeBuildPlan(p, sizedSource());
    REQUIRE(plan.finish[0].openingAssets.size() == 1);
    const OpeningAssetPlacement& fitted = plan.finish[0].openingAssets[0];
    CHECK(fitted.opening == "o1");
    CHECK(fitted.build.asset == "door_a");
    CHECK_THAT(std::fabs(fitted.build.scale.x), WithinAbs(1.0, 1e-12));
    CHECK(std::count_if(plan.assets.begin(), plan.assets.end(), [](const BuildAsset& a) { return a.id == "door_a"; }) == 1);
    CHECK(problemsWith(plan, "stretched_frame").empty());
    const Json job = checkedJob(p, plan);
    const Json& opening = job["params"]["floors"][0]["openings"][0];
    REQUIRE(opening.contains("asset"));
    CHECK(opening["asset"]["id"] == "door_a");
    CHECK(opening["asset"]["scale"].size() == 3);

    p.plan.floors[0].items.clear();
    p.plan.floors[0].openings[0].frame = "door_wide";
    plan = makeBuildPlan(p, sizedSource());
    REQUIRE(problemsWith(plan, "stretched_frame").size() == 1);
    CHECK(plan.finish[0].openingAssets.size() == 1);

    p.plan.floors[0].openings[0].frame = "door_gone";
    plan = makeBuildPlan(p, sizedSource());
    REQUIRE(problemsWith(plan, "missing_asset").size() == 1);
    CHECK(problemsWith(plan, "missing_asset")[0].object == "o1");
    CHECK(problemsWith(plan, "missing_asset")[0].severity == BuildSeverity::Error);
    CHECK(plan.finish[0].openingAssets.empty());
    CHECK_FALSE(checkedJob(p, plan)["params"]["floors"][0]["openings"][0].contains("asset"));

    p.plan.floors[0].openings[0].frame = "lamp";
    plan = makeBuildPlan(p, sizedSource());
    CHECK(problemsWith(plan, "asset_broken").size() == 1);
    CHECK(plan.finish[0].openingAssets.empty());
}

TEST_CASE("with choices, doors are picked and rooms are dressed", "[buildjob][finish]")
{
    Project p = finishProject();
    Item shelf = test::buildItem("i1", "shelf", 1.0, 0.5, {0.8, 0.3, 1.8});
    p.plan.floors[0].items = {shelf};
    BuildChoices choices;
    AssetChoice door;
    door.id = "door_a";
    door.categories = {"door"};
    door.size = {0.9, 0.05, 2.1};
    AssetChoice shelfChoice;
    shelfChoice.id = "shelf";
    shelfChoice.categories = {"shelf"};
    shelfChoice.size = {0.8, 0.3, 1.8};
    shelfChoice.supports = {{-0.38, -0.14, 0.38, 0.14, 0.02, 0.4}, {-0.38, -0.14, 0.38, 0.14, 0.47, 0.4}};
    AssetChoice books;
    books.id = "books";
    books.categories = {"decor"};
    books.tags = {"book"};
    books.size = {0.1, 0.2, 0.25};
    choices.assets = {books, door, shelfChoice};

    BuildPlan plan = makeBuildPlan(p, sizedSource(), {}, nullptr, std::nullopt, &choices);
    REQUIRE(plan.finish[0].openingAssets.size() == 1);
    CHECK(plan.finish[0].openingAssets[0].build.asset == "door_a");
    REQUIRE_FALSE(plan.finish[0].dressing.empty());
    for (const DressingPlacement& prop : plan.finish[0].dressing) {
        CHECK(prop.asset == "books");
        CHECK(prop.room == "r1");
        CHECK(prop.host == "i1");
    }
    CHECK(std::any_of(plan.assets.begin(), plan.assets.end(), [](const BuildAsset& a) { return a.id == "books"; }));
    const Json job = checkedJob(p, plan);
    const Json& dressing = job["params"]["floors"][0]["dressing"];
    REQUIRE_FALSE(dressing.empty());
    CHECK(dressing[0]["id"] == "r1_d01");
    CHECK(dressing[0]["rule"] == "books");

    p.plan.floors[0].openings[0].autoFrame = false;
    p.plan.floors[0].rooms[0].dressing = false;
    plan = makeBuildPlan(p, sizedSource(), {}, nullptr, std::nullopt, &choices);
    CHECK(plan.finish[0].openingAssets.empty());
    CHECK(plan.finish[0].dressing.empty());

    const BuildPlan without = makeBuildPlan(finishProject(), sizedSource());
    CHECK(without.finish[0].openingAssets.empty());
    CHECK(without.finish[0].dressing.empty());
}

TEST_CASE("the kit carries a door's catalog asset and tells doors with other assets apart", "[kit][finish]")
{
    Project p = finishProject();
    Opening second = test::doorOn("w3", 2.0);
    second.id = "o9";
    p.plan.floors[0].openings.push_back(second);
    const auto openingPieces = [](const ModularKit& kit) {
        std::vector<const KitPiece*> out;
        for (const KitPiece& piece : kit.pieces) {
            if (piece.kind == KitPieceKind::Opening) out.push_back(&piece);
        }
        return out;
    };

    const std::size_t plain = openingPieces(modularKit(p, makeBuildPlan(p, sizedSource()))).size();
    p.plan.floors[0].openings[0].frame = "door_a";
    p.plan.floors[0].openings[1].frame = "door_a";
    BuildPlan plan = makeBuildPlan(p, sizedSource());
    ModularKit kit = modularKit(p, plan);
    std::vector<const KitPiece*> pieces = openingPieces(kit);
    REQUIRE_FALSE(pieces.empty());
    bool carries = false;
    for (const KitPiece* piece : pieces) {
        for (const KitOpening& o : piece->openings) {
            if (o.asset) {
                carries = true;
                CHECK(o.asset->asset == "door_a");
            }
        }
    }
    CHECK(carries);
    // Doors with the same asset are as alike as doors with none.
    CHECK(pieces.size() == plain);

    p.plan.floors[0].openings[1].frame = "door_wide";
    plan = makeBuildPlan(p, sizedSource());
    kit = modularKit(p, plan);
    CHECK(openingPieces(kit).size() >= 2);
}

namespace {

// Replaces every copy of a text.
std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    return text;
}

// The finishing job the Blender tests build: the square room with a catalog door, a window with a sill, a shelf
// dressed with books, a ceiling light and trim. Library paths are @LIBRARY@ and the project @PROJECT@.
std::string finishFixtureText()
{
    Project p = test::buildProject();
    Floor& f = p.plan.floors[0];
    f.rooms.resize(1);
    f.openings[0].frame = "door";
    f.openings[1].offset = 2.0;
    f.items = {test::buildItem("i1", "shelf", 2.0, 3.6, {0.8, 0.3, 1.8})};
    f.items[0].rotation = 180.0;
    LightMarker light;
    light.id = "l1";
    light.room = "r1";
    light.x = 2.0;
    light.y = 2.0;
    light.z = 2.9;
    f.lights = {light};
    const BuildAssetSource source = [](std::string_view id) -> std::optional<BuildAssetState> {
        const std::string path = "C:/plynth-fixture/library/" + std::string(id) + "/asset.blend";
        if (id == "door") return BuildAssetState{path, false, "", {}, ItemSize{0.9, 0.05, 2.1}};
        if (id == "shelf") return BuildAssetState{path, false, "", {}, ItemSize{0.8, 0.3, 1.8}};
        if (id == "books") return BuildAssetState{path, false, "", {}, ItemSize{0.3, 0.2, 0.25}};
        return std::nullopt;
    };
    BuildChoices choices;
    AssetChoice books;
    books.id = "books";
    books.categories = {"decor"};
    books.tags = {"book"};
    books.size = {0.3, 0.2, 0.25};
    AssetChoice shelf;
    shelf.id = "shelf";
    shelf.categories = {"shelf"};
    shelf.size = {0.8, 0.3, 1.8};
    for (double z : {0.02, 0.47, 0.92, 1.37}) shelf.supports.push_back({-0.38, -0.145, 0.38, 0.145, z, 0.43});
    choices.assets = {books, shelf};
    const BuildPlan plan = makeBuildPlan(p, source, {}, nullptr, std::nullopt, &choices);
    const Json job = makeBuildJob(p, plan, BuildSettings{}, "@PROJECT@", "build-fixture", "@PROJECT@/build/result.json");
    return replaced(job.dump(2) + "
", "C:/plynth-fixture/library", "@LIBRARY@");
}

// Where the finishing job for the Blender tests is kept.
std::filesystem::path finishFixtureFile()
{
    return std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "buildjob" / "from_core_finish.json";
}

}

TEST_CASE("the finishing job for the Blender tests matches the saved copy", "[buildjob][finish][fixture]")
{
    const std::string text = finishFixtureText();
    CHECK(test::schemaProblemForText("blender-job.schema.json", text) == "");
    if (!std::filesystem::exists(finishFixtureFile())) SKIP("from_core_finish.json is written by the write-fixture run");
    INFO("Regenerate with the test case \"write the finishing job for the Blender tests\"");
    REQUIRE(test::readTextFile(finishFixtureFile()) == text);
}

TEST_CASE("write the finishing job for the Blender tests", "[.write-fixture]")
{
    std::ofstream(finishFixtureFile(), std::ios::binary | std::ios::trunc) << finishFixtureText();
    REQUIRE(std::filesystem::exists(finishFixtureFile()));
}
