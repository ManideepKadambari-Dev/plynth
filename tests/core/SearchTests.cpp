#include "CatalogSupport.h"

#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace plynth;
using plynth::test::TestLibrary;
using plynth::test::ids;

namespace {

// A library with a few hand made items.
struct SearchLibrary : TestLibrary {
    SearchLibrary() : TestLibrary("search")
    {
        add("chair", "Chair", {"chair"}, {"oak", "wood"});
        add("armchair", "Armchair", {"armchair"}, {"fabric", "chair"});
        add("sofa", "Grey sofa", {"sofa"}, {"fabric"});
        add("lamp", "Floor lamp", {});
        add("fauteuil", "Fauteuil Élégant", {"armchair"}, {});
        add("oak_table", "Oak table", {"table"}, {"oak", "wood"});
    }
};

}

TEST_CASE("search matches the start of words", "[core][search]") {
    SearchLibrary lib;
    const auto& c = *lib.catalog;
    REQUIRE(ids(c.search({.text = "cha"})) == std::vector<std::string>{"armchair", "chair"});
    REQUIRE(ids(c.search({.text = "CHAIR"})) == std::vector<std::string>{"armchair", "chair"});
    REQUIRE(ids(c.search({.text = "hair"})).empty());
    REQUIRE(ids(c.search({.text = "grey so"})) == std::vector<std::string>{"sofa"});
}

TEST_CASE("every word of a search must match", "[core][search]") {
    SearchLibrary lib;
    const auto& c = *lib.catalog;
    REQUIRE(ids(c.search({.text = "oak chair"})) == std::vector<std::string>{"chair"});
    REQUIRE(ids(c.search({.text = "oak"})) == std::vector<std::string>{"chair", "oak_table"});
    REQUIRE(ids(c.search({.text = "oak sofa"})).empty());
}

TEST_CASE("search looks at the id, tags, categories and source", "[core][search]") {
    SearchLibrary lib;
    const auto& c = *lib.catalog;
    REQUIRE(ids(c.search({.text = "oak_table"})) == std::vector<std::string>{"oak_table"});
    REQUIRE(ids(c.search({.text = "fabric"})) == std::vector<std::string>{"armchair", "sofa"});
    REQUIRE(ids(c.search({.text = "table"})) == std::vector<std::string>{"oak_table"});
    REQUIRE(ids(c.search({.text = "lamp.obj"})) == std::vector<std::string>{"lamp"});
}

TEST_CASE("search ignores accents", "[core][search]") {
    SearchLibrary lib;
    const auto& c = *lib.catalog;
    REQUIRE(ids(c.search({.text = "elegant"})) == std::vector<std::string>{"fauteuil"});
    REQUIRE(ids(c.search({.text = "ÉLÉG"})) == std::vector<std::string>{"fauteuil"});
    REQUIRE(ids(c.search({.text = "fauteuil elegant"})) == std::vector<std::string>{"fauteuil"});
}

TEST_CASE("search text with odd characters does not break the query", "[core][search]") {
    SearchLibrary lib;
    const auto& c = *lib.catalog;
    REQUIRE(ids(c.search({.text = "\"cha*"})) == std::vector<std::string>{"armchair", "chair"});
    REQUIRE(ids(c.search({.text = "chair AND OR NOT ("})).empty());
    REQUIRE(c.search({.text = "   "}).size() == 6);
    REQUIRE(c.search({.text = "x'; DROP TABLE assets; --"}).empty());
    REQUIRE(c.assetCount() == 6);
}

TEST_CASE("search by room type", "[core][search]") {
    SearchLibrary lib;
    const auto& c = *lib.catalog;
    REQUIRE(ids(c.search({.roomType = "living"})) == std::vector<std::string>{"armchair", "fauteuil", "sofa", "oak_table"});
    REQUIRE(ids(c.search({.roomType = "bathroom"})).empty());
    REQUIRE(ids(c.search({.roomType = "living", .category = "sofa"})) == std::vector<std::string>{"sofa"});
    REQUIRE(ids(c.search({.text = "oak", .roomType = "living"})) == std::vector<std::string>{"oak_table"});
}

TEST_CASE("search by category, tags, license and uncategorized", "[core][search]") {
    SearchLibrary lib;
    auto& c = *lib.catalog;
    REQUIRE(ids(c.search({.category = "armchair"})) == std::vector<std::string>{"armchair", "fauteuil"});
    REQUIRE(ids(c.search({.tags = {"oak", "wood"}})) == std::vector<std::string>{"chair", "oak_table"});
    REQUIRE(ids(c.search({.tags = {"oak", "fabric"}})).empty());
    REQUIRE(ids(c.search({.uncategorizedOnly = true})) == std::vector<std::string>{"lamp"});
    plynth::test::requireOk(c.editAsset("sofa", AssetEdit{.license = "cc0"}));
    REQUIRE(ids(c.search({.license = "cc0"})) == std::vector<std::string>{"sofa"});
    REQUIRE(c.search({.license = "unknown"}).size() == 5);
}

TEST_CASE("search is sorted by name and has a limit", "[core][search]") {
    SearchLibrary lib;
    const auto& c = *lib.catalog;
    const auto all = c.search({});
    REQUIRE(all.size() == 6);
    REQUIRE(all.front().name == "Armchair");
    REQUIRE(all.back().name == "Oak table");
    REQUIRE(c.search({.limit = 2}).size() == 2);
    REQUIRE(c.search({.text = "oak", .limit = 1}).size() == 1);
}

TEST_CASE("search finds renamed assets and categories", "[core][search]") {
    SearchLibrary lib;
    auto& c = *lib.catalog;
    plynth::test::requireOk(c.editAsset("lamp", AssetEdit{.name = "Reading light"}));
    REQUIRE(ids(c.search({.text = "lamp"})) == std::vector<std::string>{"lamp"});
    REQUIRE(ids(c.search({.text = "reading"})) == std::vector<std::string>{"lamp"});
    plynth::test::requireOk(c.renameCategory("sofa", "Couch"));
    REQUIRE(ids(c.search({.text = "couch"})) == std::vector<std::string>{"sofa"});
}
