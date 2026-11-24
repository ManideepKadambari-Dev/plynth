#include "StoreTestRig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace plynth;

namespace {

using Names = std::vector<std::string>;

// A set of textures, the limit and what has to be taken of it.
struct Case {
    const char* name;
    Names textures;
    std::optional<int> limit;
    Names taken;
    std::optional<int> largest;
};

// Every resolution case of the review and the contract.
std::vector<Case> cases()
{
    const Names mixed{"T_A_BaseColor_4K.png", "T_A_Normal_4K.png", "T_A_Roughness_2K.png"};
    const Names two{"Wood_BaseColor_2K.png", "Wood_BaseColor_4K.png", "Glass_BaseColor_2K.png"};
    const Names folders{"1K/Albedo.png", "2K/Albedo.png", "1K/Normal.png"};
    return {
        {"mixed", mixed, 4096, mixed, 4096},
        {"two materials", two, 4096, {"Glass_BaseColor_2K.png", "Wood_BaseColor_4K.png"}, 4096},
        {"folders", folders, 4096, {"1K/Normal.png", "2K/Albedo.png"}, 2048},
        {"mixed, all larger", mixed, 1024, mixed, 4096},
        {"two materials, all larger", two, 1024, {"Glass_BaseColor_2K.png", "Wood_BaseColor_2K.png"}, 2048},
        {"limit between", two, 3000, {"Glass_BaseColor_2K.png", "Wood_BaseColor_2K.png"}, 2048},
        {"no limit", {"Wood_BaseColor_8K.png", "Wood_BaseColor_4K.png", "Glass_BaseColor_2K.png"}, std::nullopt,
         {"Glass_BaseColor_2K.png", "Wood_BaseColor_8K.png"}, 8192},
        {"case, separators and endings", {"T-Wood-BaseColor-2K.jpg", "t_wood_basecolor_4k.png"}, 4096,
         {"t_wood_basecolor_4k.png"}, 4096},
        {"word first", {"2K_Wood_Normal.png", "4K_Wood_Normal.png", "1K_Wood_Roughness.png"}, 4096,
         {"1K_Wood_Roughness.png", "4K_Wood_Normal.png"}, 4096},
        {"untagged kept", {"Wood_BaseColor_2K.png", "Wood_BaseColor_4K.png", "Wood_Roughness.png"}, 4096,
         {"Wood_BaseColor_4K.png", "Wood_Roughness.png"}, 4096},
        {"same name in two folders", {"Seat/BaseColor_2K.png", "Legs/BaseColor_4K.png"}, 4096,
         {"Legs/BaseColor_4K.png", "Seat/BaseColor_2K.png"}, 4096}};
}

}

TEST_CASE("each texture has its own resolution in a model zip", "[fab][store_resolution]")
{
    for (const Case& c : cases()) {
        INFO(c.name);
        Names names = c.textures;
        names.push_back("Thing.fbx");
        FabOptions options;
        options.maxTextureSize = c.limit;
        const FabChoice choice = chooseFabModel(plynth::test::fabContentsOf(names), options);
        plynth::test::requireOk(choice.status);
        Names taken = c.taken;
        std::sort(taken.begin(), taken.end());
        CHECK(choice.textures == taken);
        CHECK(choice.textureResolution == c.largest);
        for (const std::string& texture : taken) {
            CHECK(std::find(choice.files.begin(), choice.files.end(), texture) != choice.files.end());
        }
        CHECK(choice.skipped.size() == c.textures.size() - taken.size());
    }
}

TEST_CASE("each texture has its own resolution in a material zip", "[fab][store_resolution]")
{
    for (const Case& c : cases()) {
        INFO(c.name);
        FabOptions options;
        options.maxTextureSize = c.limit;
        const FabChoice choice = chooseFabMaterial(plynth::test::fabContentsOf(c.textures), options);
        plynth::test::requireOk(choice.status);
        Names taken = c.taken;
        std::sort(taken.begin(), taken.end());
        CHECK(choice.textures == taken);
        CHECK(choice.textureResolution == c.largest);
        CHECK(choice.skipped.size() == c.textures.size() - taken.size());
    }
}

TEST_CASE("a texture picked by hand is added beside the resolution that was taken", "[fab][store_resolution]")
{
    FabOptions options;
    options.maps.push_back(FabMapPick{"Wood", MapChannel::BaseColor, "Wood_BaseColor_2K.png"});
    const FabChoice choice =
        chooseFabModel(plynth::test::fabContentsOf({"Thing.fbx", "Wood_BaseColor_2K.png", "Wood_BaseColor_4K.png"}), options);
    CHECK(choice.textures == Names{"Wood_BaseColor_2K.png", "Wood_BaseColor_4K.png"});
}
