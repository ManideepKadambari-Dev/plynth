#include <plynth/core/catalog/BlenderKit.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace plynth;

namespace {

const std::string kId = "0c1d4a2e-1111-2222-3333-444455556666";
const std::string kOther = "aaaaaaaa-1111-2222-3333-444455556666";
const char* const kNotAddress = "This is not a BlenderKit address.";
const char* const kNoItem = "The address does not name one BlenderKit item. Copy it from the item's page.";
const char* const kOnlyKinds = "Only models and materials can be imported from BlenderKit.";

// Checks that a text gives the item id.
void requireItem(const std::string& text)
{
    INFO(text);
    const BlenderKitAddress address = parseBlenderKitAddress(text);
    REQUIRE(address.status.ok());
    REQUIRE(address.assetBaseId == kId);
}

// Checks that a text is refused with a sentence.
void requireRefused(const std::string& text, const char* sentence)
{
    INFO(text);
    const BlenderKitAddress address = parseBlenderKitAddress(text);
    REQUIRE(address.status.code == ErrorCode::InvalidValue);
    REQUIRE(address.status.message == sentence);
}

}

TEST_CASE("W9: both hosts are read, the new one is the site", "[core][blenderkit][w9]")
{
    REQUIRE(kBlenderKitSite == "https://www.blendkit.com/");
    for (const std::string host : {"https://www.blendkit.com", "https://blendkit.com", "http://www.blendkit.com",
                                   "https://www.blenderkit.com", "https://blenderkit.com", "HTTPS://WWW.BLENDKIT.COM"}) {
        requireItem(host + "/get-blendkit/" + kId + "/");
        requireItem(host + "/asset-gallery-detail/" + kId + "/?utm_source=x");
    }
}

TEST_CASE("W9: look alike hosts are refused", "[core][blenderkit][w9]")
{
    for (const std::string host : {"https://www.blendkit.com.lookalike.invalid", "https://wwwblendkit.invalid",
                                   "https://blendkit.invalid", "https://notblendkit.invalid", "https://blendkit.com@other.invalid",
                                   "https://other.invalid/www.blendkit.com", "ftp://www.blendkit.com"}) {
        requireRefused(host + "/asset-gallery-detail/" + kId + "/", kNotAddress);
    }
}

TEST_CASE("W10: the copied forms of the add-on's search box", "[core][blenderkit][w10]")
{
    requireItem("asset_base_id:" + kId);
    requireItem("  asset_base_id:" + kId + "  ");
    requireItem("Asset_Base_ID:" + kId + " Asset_Type:Model");
    requireItem("asset_base_id:" + kId + " asset_type:material");
    requireItem("asset_base_id:" + kId + " some words between asset_type:model");
    requireItem(kId);
}

TEST_CASE("W10, W2: a copied type other than model or material starts no Blender", "[core][blenderkit][w10][w2]")
{
    for (const std::string type : {"addon", "ADDON", "add-on", "hdr", "scene", "brush", "printable", "nodegroup"}) {
        requireRefused("asset_base_id:" + kId + " asset_type:" + type, kOnlyKinds);
    }
}

TEST_CASE("W10: two different ids are refused", "[core][blenderkit][w10]")
{
    requireRefused("asset_base_id:" + kId + " asset_base_id:" + kOther, kNoItem);
    requireRefused("https://www.blendkit.com/a/" + kId + "/b/" + kOther + "/", kNoItem);
}
