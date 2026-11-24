#include "SchemaSupport.h"

#include <plynth/core/Choices.h>
#include <plynth/core/Dressing.h>
#include <plynth/core/GameplayData.h>
#include <plynth/core/OpeningAssets.h>
#include <plynth/core/Trim.h>
#include <plynth/core/Variation.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;

TEST_CASE("The finishing types start empty and compare by value", "[choice]")
{
    CHECK(Sweep{} == Sweep{});
    CHECK_FALSE(Sweep{}.closed);
    CHECK(TrimBuild{}.kind == TrimKind::Skirting);
    const OpeningAssetBuild placed{};
    CHECK(placed.scale == Vec3{1.0, 1.0, 1.0});
    CHECK(BuildChoices{} == BuildChoices{});
    CHECK(DressingPlacement{} == DressingPlacement{});
    CHECK(VariationChange{}.target == VariationTarget::Item);
    CHECK(GameplayData{} == GameplayData{});
    CHECK(kGameplayVersion == 1);
    CHECK(kMaxDressingPerRoom == 40);
    CHECK(VariationSettings{}.seed == 0u);
}

TEST_CASE("The finishing fields of a build job floor match their schema", "[choice][schema]")
{
    const char* text = R"({
      "openings": [{"asset": {"id": "door_01", "location": [0, 0, 0], "rotation_z": 0.0, "scale": [-1.02, 1.0, 0.99]}}],
      "trim": [{"kind": "skirting", "plan_id": "f1", "object": "SM_f1_Skirting", "plynth_kind": "trim",
                "slot": "Trim", "material": "painted_wood",
                "sweeps": [{"closed": false, "sections": [[[0, 0, 0], [0.01, 0, 0], [0, 0, 0.1]],
                                                          [[1, 0, 0], [1.01, 0, 0], [1, 0, 0.1]]]}]}],
      "dressing": [{"id": "r1_d01", "room": "r1", "rule": "books", "host": "i4", "asset": "books",
                    "location": [0, 0, 0], "rotation_z": 0.0, "size": {"width": 0.3, "depth": 0.2, "height": 0.25}}],
      "lights": [{"id": "l1", "kind": "ceiling", "location": [0, 0, 2.9], "power": 60, "color": [1.0, 0.9, 0.8]}]
    })";
    CHECK(plynth::test::schemaProblemForText("blender-job-finish.schema.json", text) == "");
    nlohmann::json bad = nlohmann::json::parse(text);
    bad["lights"][0]["power"] = 0;
    CHECK(plynth::test::schemaProblem("blender-job-finish.schema.json", bad) != "");
    CHECK(plynth::test::schemaProblem("blender-job-finish.schema.json", nlohmann::json::object()) == "");
}
