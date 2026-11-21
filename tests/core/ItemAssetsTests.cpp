#include "CatalogSupport.h"

#include <plynth/core/catalog/ItemAssets.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace plynth;

namespace {

// Writes info.json again with another size and indexes it.
void remeasure(plynth::test::TestLibrary& lib, const std::string& id, AssetSize size)
{
    AssetInfo info = readAssetInfo(lib.paths.assetItems / id / "info.json", id).info;
    info.size = size;
    plynth::test::requireOk(writeAssetInfo(lib.paths.assetItems / id / "info.json", info));
    plynth::test::requireOk(lib.catalog->indexAsset(info));
}

// The ids of categories.
std::vector<std::string> categoryIds(const std::vector<CategoryEntry>& list)
{
    std::vector<std::string> out;
    for (const CategoryEntry& c : list) out.push_back(c.id);
    return out;
}

}

TEST_CASE("isMeasured needs every side above zero", "[core][catalog][items]") {
    REQUIRE(isMeasured({1.0, 2.0, 3.0}));
    REQUIRE_FALSE(isMeasured({}));
    REQUIRE_FALSE(isMeasured({1.0, 0.0, 3.0}));
    REQUIRE_FALSE(isMeasured({1.0, 2.0, -3.0}));
}

TEST_CASE("itemSizeFrom copies the catalog size", "[core][catalog][items]") {
    const ItemSize s = itemSizeFrom({2.0, 0.9, 0.85});
    REQUIRE(s == ItemSize{2.0, 0.9, 0.85});
}

TEST_CASE("itemAssetFor says whether an asset can be placed", "[core][catalog][items]") {
    plynth::test::TestLibrary lib("item-assets");
    lib.add("sofa_01", "Sofa", {"sofa"});
    lib.add("flat", "Flat", {"decor"});
    remeasure(lib, "flat", {0.0, 0.0, 0.0});
    lib.add("old", "Old chair", {"chair"});
    // A changed asset.blend makes the asset NeedsRefresh while info.json keeps its size.
    plynth::test::writeBytes(lib.paths.assetItems / "old" / "asset.blend", "CHANGED-BLEND-FILE");
    lib.add("gone", "Gone", {"decor"});
    std::filesystem::remove(lib.paths.assetItems / "gone" / "asset.blend");
    lib.catalog->rescan();

    SECTION("a ready asset gives its size and name") {
        const ItemAsset a = itemAssetFor(*lib.catalog, "sofa_01");
        plynth::test::requireOk(a.status);
        REQUIRE(a.name == "Sofa");
        REQUIRE(a.size == ItemSize{0.5, 0.5, 0.9});
    }
    SECTION("an unknown asset is not found") {
        const ItemAsset a = itemAssetFor(*lib.catalog, "sofa_99");
        REQUIRE(a.status.code == ErrorCode::NotFound);
        REQUIRE(a.status.message == "There is no asset sofa_99 in the library.");
    }
    SECTION("an unmeasured asset is refused with its name") {
        const ItemAsset a = itemAssetFor(*lib.catalog, "flat");
        REQUIRE(a.status.code == ErrorCode::InvalidValue);
        REQUIRE(a.status.message.find("not measured") != std::string::npos);
        REQUIRE(a.status.message.find("Flat") != std::string::npos);
        REQUIRE(a.status.message.find("flat") != std::string::npos);
    }
    SECTION("a broken asset is refused") {
        const auto record = lib.catalog->asset("gone");
        REQUIRE(record);
        REQUIRE(record->state == AssetState::Broken);
        const ItemAsset a = itemAssetFor(*lib.catalog, "gone");
        REQUIRE(a.status.code == ErrorCode::InvalidValue);
        REQUIRE_FALSE(a.status.message.empty());
    }
    SECTION("an asset that needs a refresh can be placed") {
        const auto record = lib.catalog->asset("old");
        REQUIRE(record);
        REQUIRE(record->state == AssetState::NeedsRefresh);
        const ItemAsset a = itemAssetFor(*lib.catalog, "old");
        plynth::test::requireOk(a.status);
        REQUIRE(a.size == ItemSize{0.5, 0.5, 0.9});
    }
}

TEST_CASE("placingCategories follows the room type", "[core][catalog][items]") {
    plynth::test::TestLibrary lib("item-categories");
    const std::vector<std::string> living = categoryIds(placingCategories(*lib.catalog, "living"));
    REQUIRE(living == std::vector<std::string>{"sofa", "armchair", "table", "shelf", "tv", "lamp", "carpet", "curtain", "plant",
                                               "decor"});
    const std::vector<std::string> every = categoryIds(lib.catalog->categories());
    REQUIRE(categoryIds(placingCategories(*lib.catalog, "other")) == every);
    REQUIRE(categoryIds(placingCategories(*lib.catalog, "")) == every);
    REQUIRE(categoryIds(placingCategories(*lib.catalog, "no_such_type")) == every);
    REQUIRE(every.size() > living.size());
}

TEST_CASE("assetLookupFor gives the library size and nothing for a missing asset", "[core][catalog][items]") {
    plynth::test::TestLibrary lib("item-lookup");
    lib.add("sofa_01", "Sofa", {"sofa"});
    const AssetLookup lookup = assetLookupFor(*lib.catalog);
    const std::optional<ItemSize> size = lookup("sofa_01");
    REQUIRE(size);
    REQUIRE(*size == ItemSize{0.5, 0.5, 0.9});
    REQUIRE_FALSE(lookup("nothing"));
    remeasure(lib, "sofa_01", {2.0, 0.9, 0.85});
    REQUIRE(*lookup("sofa_01") == ItemSize{2.0, 0.9, 0.85});
}
