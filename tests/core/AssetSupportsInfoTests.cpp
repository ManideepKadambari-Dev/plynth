#include "CatalogSupport.h"
#include "SchemaSupport.h"

#include <plynth/core/catalog/AssetInfo.h>
#include <plynth/core/catalog/BuildChoices.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;

namespace {

// The info.json file of an asset of a test library.
std::filesystem::path infoFile(const TestLibrary& lib, const std::string& id)
{
    return lib.paths.assetItems / pathFromUtf8(id) / "info.json";
}

}

TEST_CASE("supports round trip through info.json and an old file reads as not measured", "[assetsupports][catalog]")
{
    TestLibrary lib("supports-info");
    AssetInfo info = makeAssetOnDisk(lib.paths, "shelf", "Shelf", {"shelf"});
    info.supports = std::vector<AssetSupport>{{-0.38, -0.14, 0.38, 0.14, 0.02, 0.43}, {-0.38, -0.14, 0.38, 0.14, 0.47, 0.43}};
    requireOk(writeAssetInfo(infoFile(lib, "shelf"), info));
    const AssetInfoRead read = readAssetInfo(infoFile(lib, "shelf"), "shelf");
    requireOk(read.status);
    CHECK(read.info == info);
    REQUIRE(read.info.supports.has_value());
    CHECK(read.info.supports->size() == 2);
    CHECK(schemaProblem("asset-info.schema.json", nlohmann::json::parse(readTextFile(infoFile(lib, "shelf")))) == "");

    info.supports.reset();
    requireOk(writeAssetInfo(infoFile(lib, "shelf"), info));
    CHECK(readTextFile(infoFile(lib, "shelf")).find("\"supports\"") == std::string::npos);
    const AssetInfoRead old = readAssetInfo(infoFile(lib, "shelf"), "shelf");
    requireOk(old.status);
    CHECK_FALSE(old.info.supports.has_value());
}

TEST_CASE("bad supports in info.json are refused", "[assetsupports][catalog]")
{
    TestLibrary lib("supports-bad");
    makeAssetOnDisk(lib.paths, "table", "Table", {"table"});
    nlohmann::json json = nlohmann::json::parse(readTextFile(infoFile(lib, "table")));
    json["supports"] = nlohmann::json::array({{{"x0", 0.5}, {"y0", 0.0}, {"x1", 0.1}, {"y1", 0.3}, {"z", 0.7}, {"clearance", 0.5}}});
    std::ofstream(infoFile(lib, "table"), std::ios::binary) << json.dump(2);
    const AssetInfoRead read = readAssetInfo(infoFile(lib, "table"), "table");
    CHECK(read.status.code == ErrorCode::BadFile);
    CHECK(read.status.where == "/supports");
}

TEST_CASE("rescan lists an asset not measured for supports once", "[assetsupports][catalog]")
{
    TestLibrary lib("supports-rescan");
    AssetInfo info = makeAssetOnDisk(lib.paths, "lamp", "Lamp");
    makeAssetOnDisk(lib.paths, "sofa", "Sofa");
    info.supports.reset();
    requireOk(writeAssetInfo(infoFile(lib, "lamp"), info));
    const RescanReport report = lib.catalog->rescan();
    CHECK(report.needsRefresh == std::vector<std::string>{"lamp"});
}

TEST_CASE("build choices carry the measured supports, sorted by id", "[assetsupports][catalog][resolve]")
{
    TestLibrary lib("supports-choices");
    AssetInfo shelf = makeAssetOnDisk(lib.paths, "shelf", "Shelf", {"shelf"});
    shelf.supports = std::vector<AssetSupport>{{-0.38, -0.14, 0.38, 0.14, 0.02, 0.43}};
    requireOk(writeAssetInfo(infoFile(lib, "shelf"), shelf));
    makeAssetOnDisk(lib.paths, "books", "Books", {"decor"}, {"book"});
    makeAssetOnDisk(lib.paths, "armchair", "Armchair", {"chair"});
    lib.catalog->rescan();

    const BuildChoices choices = buildChoicesFrom(lib.catalog.get(), nullptr);
    std::vector<std::string> ids;
    for (const AssetChoice& c : choices.assets) ids.push_back(c.id);
    CHECK(ids == std::vector<std::string>{"armchair", "books", "shelf"});
    const auto found = std::find_if(choices.assets.begin(), choices.assets.end(), [](const AssetChoice& c) { return c.id == "shelf"; });
    REQUIRE(found != choices.assets.end());
    CHECK(found->supports == *shelf.supports);
    CHECK(found->categories == std::vector<std::string>{"shelf"});
    CHECK(choices.materials.empty());
}
