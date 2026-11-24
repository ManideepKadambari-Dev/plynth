#include "SchemaSupport.h"

#include <plynth/core/catalog/AssetSupports.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

using namespace plynth;
using plynth::test::schemaProblem;

namespace {

// Two good supports: a shelf board and a top.
std::vector<AssetSupport> twoSupports()
{
    return {{-0.4, -0.15, 0.4, 0.15, 0.35, 0.3}, {-0.45, -0.2, 0.45, 0.2, 1.8, 0.5}};
}

}

TEST_CASE("Supports round trip through JSON", "[assetsupports]")
{
    const std::vector<AssetSupport> empty;
    const Json emptyJson = assetSupportsToJson(empty);
    CHECK(emptyJson.is_array());
    CHECK(emptyJson.empty());
    REQUIRE(assetSupportsFromJson(emptyJson).has_value());
    CHECK(assetSupportsFromJson(emptyJson)->empty());

    const std::vector<AssetSupport> two = twoSupports();
    const Json twoJson = assetSupportsToJson(two);
    REQUIRE(twoJson.size() == 2);
    CHECK(twoJson[0]["clearance"].get<double>() == 0.3);
    REQUIRE(assetSupportsFromJson(twoJson).has_value());
    CHECK(*assetSupportsFromJson(twoJson) == two);

    // No list at all is not a list of supports.
    CHECK_FALSE(assetSupportsFromJson(Json()).has_value());
    CHECK_FALSE(assetSupportsFromJson(Json::object()).has_value());
}

TEST_CASE("Bad supports are refused", "[assetsupports]")
{
    CHECK(checkAssetSupports(twoSupports()).ok());

    auto refuses = [](AssetSupport s) {
        std::vector<AssetSupport> list = twoSupports();
        list.push_back(s);
        const Status status = checkAssetSupports(list);
        CHECK(status.code == ErrorCode::InvalidValue);
        CHECK(status.where == "/2");
        CHECK_FALSE(assetSupportsFromJson(assetSupportsToJson(list)).has_value());
    };
    refuses({0.4, -0.15, -0.4, 0.15, 0.3, 0.3});                                      // reversed
    refuses({-0.4, std::numeric_limits<double>::quiet_NaN(), 0.4, 0.15, 0.3, 0.3});   // NaN
    refuses({-0.025, -0.15, 0.025, 0.15, 0.3, 0.3});                                  // 0.05 m side
    refuses({-0.4, -0.15, 0.4, 0.15, 0.3, 0.05});                                     // low clearance

    std::vector<AssetSupport> many(17, AssetSupport{-0.1, -0.1, 0.1, 0.1, 0.5, 0.2});
    CHECK(checkAssetSupports(many).code == ErrorCode::InvalidValue);
    many.pop_back();
    CHECK(checkAssetSupports(many).ok());

    // Exactly 0.1 m on a side and 0.1 m free above is enough.
    CHECK(checkAssetSupports({{-0.05, -0.05, 0.05, 0.05, 0.0, 0.1}}).ok());

    Json missing = assetSupportsToJson(twoSupports());
    missing[0].erase("z");
    CHECK_FALSE(assetSupportsFromJson(missing).has_value());
    Json text = assetSupportsToJson(twoSupports());
    text[1]["x0"] = "left";
    CHECK_FALSE(assetSupportsFromJson(text).has_value());
}

TEST_CASE("Written supports match their schema", "[assetsupports][schema]")
{
    const Json written = assetSupportsToJson(twoSupports());
    CHECK(schemaProblem("asset-supports.schema.json", nlohmann::json::parse(written.dump())) == "");
    CHECK(schemaProblem("asset-supports.schema.json", nlohmann::json::array()) == "");
    nlohmann::json extra = nlohmann::json::parse(written.dump());
    extra[0]["colour"] = "red";
    CHECK(schemaProblem("asset-supports.schema.json", extra) != "");
}
