#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/Integrations.h>

#include "SchemaSupport.h"
#include "TestSupport.h"

#include <catch2/catch_test_macros.hpp>
#include <plynth/core/BlenderJob.h>

using namespace plynth;

namespace {

const char* const kId = "0c1d4a2e-1111-2222-3333-444455556666";
const char* const kOtherId = "aaaaaaaa-1111-2222-3333-444455556666";
const char* const kNotAddress = "This is not a BlenderKit address.";
const char* const kNoItem = "The address does not name one BlenderKit item. Copy it from the item's page.";

}

TEST_CASE("a gallery page address gives the lower case id", "[core][blenderkit]") {
    const BlenderKitAddress a =
        parseBlenderKitAddress("https://www.blenderkit.com/asset-gallery-detail/0C1D4A2E-1111-2222-3333-444455556666/");
    REQUIRE(a.status.ok());
    REQUIRE(a.assetBaseId == kId);
}

TEST_CASE("a Get link is refused with the sentence that names the forms that work", "[core][blenderkit]") {
    const std::string sentence = "This is a Get link, which holds a version id Plynth cannot look up. Paste the address "
                                 "of the item's page (blenderkit.com/asset-gallery-detail/...) or copy the asset ID in "
                                 "BlenderKit inside Blender.";
    for (const std::string text : {std::string("https://www.blendkit.com/get-blendkit/") + kId + "/",
                                   std::string("https://www.blenderkit.com/get-blenderkit/") + kId + "/?from_addon=True",
                                   std::string("  HTTPS://BLENDKIT.COM/GET-BLENDKIT/") + kId + "  ",
                                   std::string("http://blenderkit.com/get-blenderkit/") + kId,
                                   std::string("https://www.blendkit.com/get-blendkit/not-an-id/")}) {
        INFO(text);
        const BlenderKitAddress a = parseBlenderKitAddress(text);
        REQUIRE(a.status.code == ErrorCode::InvalidValue);
        REQUIRE(a.status.message == sentence);
        REQUIRE(a.assetBaseId.empty());
    }
    // Only the first part of the path makes a Get link.
    const BlenderKitAddress later = parseBlenderKitAddress(std::string("https://www.blendkit.com/asset-gallery-detail/") + kId +
                                                           "/get-blendkit/");
    REQUIRE(later.status.ok());
}

TEST_CASE("other address forms work", "[core][blenderkit]") {
    const BlenderKitAddress gallery = parseBlenderKitAddress(
        std::string("http://blenderkit.com/asset-gallery-detail/") + kId + "/?x=1");
    REQUIRE(gallery.status.ok());
    REQUIRE(gallery.assetBaseId == kId);

    const BlenderKitAddress prefixed = parseBlenderKitAddress(std::string("asset_base_id:") + kId);
    REQUIRE(prefixed.status.ok());
    REQUIRE(prefixed.assetBaseId == kId);

    const BlenderKitAddress bare = parseBlenderKitAddress(std::string("  ") + kId + " \n");
    REQUIRE(bare.status.ok());
    REQUIRE(bare.assetBaseId == kId);

    const BlenderKitAddress fragment = parseBlenderKitAddress(std::string("https://www.blenderkit.com/x/") + kId + "#top");
    REQUIRE(fragment.status.ok());
    REQUIRE(fragment.assetBaseId == kId);

    const BlenderKitAddress same = parseBlenderKitAddress(std::string("https://www.blenderkit.com/a/") + kId + "/b/" + kId);
    REQUIRE(same.status.ok());
}

TEST_CASE("another site is not a BlenderKit address", "[core][blenderkit]") {
    for (const std::string text : {std::string("https://www.example.com/") + kId + "/",
                                   std::string("https://blenderkit.com.example.com/") + kId + "/",
                                   std::string("https://example.com/blenderkit.com/") + kId, std::string("hello"),
                                   std::string("ftp://www.blenderkit.com/") + kId, std::string("")}) {
        INFO(text);
        const BlenderKitAddress a = parseBlenderKitAddress(text);
        REQUIRE(a.status.code == ErrorCode::InvalidValue);
        REQUIRE(a.status.message == kNotAddress);
        REQUIRE(a.assetBaseId.empty());
    }
}

TEST_CASE("a BlenderKit address without one id is refused", "[core][blenderkit]") {
    for (const std::string text : {std::string("https://www.blenderkit.com/"), std::string("https://www.blenderkit.com"),
                                   std::string("https://www.blenderkit.com/asset-gallery-detail/not-an-id/"),
                                   std::string("https://www.blenderkit.com/a/") + kId + "/b/" + kOtherId,
                                   std::string("https://www.blenderkit.com/?id=") + kId,
                                   std::string("asset_base_id:nonsense")}) {
        INFO(text);
        const BlenderKitAddress a = parseBlenderKitAddress(text);
        REQUIRE(a.status.code == ErrorCode::InvalidValue);
        REQUIRE(a.status.message == kNoItem);
    }
}

TEST_CASE("resolutions are the add-on's", "[core][blenderkit]") {
    for (const char* r : {"0.5k", "1k", "2k", "4k", "8k", "original"}) REQUIRE(isBlenderKitResolution(r));
    REQUIRE_FALSE(isBlenderKitResolution("3k"));
    REQUIRE_FALSE(isBlenderKitResolution(""));
}

TEST_CASE("blenderKitOptionsFromJson reads and refuses", "[core][blenderkit]") {
    BlenderKitOptions defaults;
    REQUIRE(defaults.resolution == "2k");
    REQUIRE(defaults.waitSeconds == 600);

    BlenderKitOptions options;
    plynth::test::requireOk(blenderKitOptionsFromJson(
        Json{{"blender_window", "minimized"}, {"resolution", "4k"}, {"wait_seconds", 90}}, options));
    REQUIRE(blenderKitOptionsFromJson(Json{{"blender_window", "none"}}, options).ok());
    REQUIRE(options.resolution == "4k");
    REQUIRE(options.waitSeconds == 90);

    for (const Json& bad : {Json{{"resolution", "3k"}}, Json{{"wait_seconds", 10}}, Json{{"wait_seconds", 4000}},
                            Json{{"wait_seconds", 90.5}}, Json{{"blender_window", "maximized"}},
                            Json{{"unknown", 1}}, Json("text")}) {
        INFO(bad.dump());
        BlenderKitOptions out;
        out.resolution = "1k";
        const Status s = blenderKitOptionsFromJson(bad, out);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(out.resolution == "1k");
    }
    BlenderKitOptions out;
    const Status s = blenderKitOptionsFromJson(Json{{"resolution", "3k"}}, out);
    REQUIRE(s.message.find("resolution") != std::string::npos);
}

TEST_CASE("a saved minimized window is read without error and the integration offers no window", "[core][blenderkit]") {
    BlenderKitOptions options;
    plynth::test::requireOk(blenderKitOptionsFromJson(Json{{"blender_window", "minimized"}}, options));
    const Integration* integration = findIntegration("blenderkit");
    REQUIRE(integration != nullptr);
    REQUIRE_FALSE(integration->defaultOptions().contains("blender_window"));
    plynth::test::requireOk(integration->checkOptions(Json{{"blender_window", "minimized"}}, OptionUse::Saved));
    const Json saved = Json{{"blenderkit", Json{{"blender_window", "minimized"}, {"resolution", "4k"}}}};
    const Json effective = integrationOptions(*integration, saved);
    REQUIRE_FALSE(effective.contains("blender_window"));
    REQUIRE(effective["resolution"] == "4k");
    const Json listed = integrationToJson(*integration, integrationStatus(*integration, nullptr), effective);
    REQUIRE(listed["note"] == "Downloads run Blender with your own settings and enabled add-ons.");
}

TEST_CASE("makeBlenderKitFetch builds the task", "[core][blenderkit]") {
    BlenderKitOptions options;
    options.resolution = "1k";
    options.waitSeconds = 600;
    ImportFetch fetch = makeBlenderKitFetch(kId, kFetchKindModel, options);
    REQUIRE(fetch.task == "blenderkit_fetch");
    REQUIRE(fetch.expectedKind == "model");
    REQUIRE(fetch.timeout == std::chrono::seconds(720));
    REQUIRE(fetch.params["asset_base_id"] == kId);
    REQUIRE(fetch.params["kind"] == "model");
    REQUIRE(fetch.params["resolution"] == "1k");
    REQUIRE(fetch.params["wait_seconds"] == 600);
    REQUIRE_FALSE(fetch.params.contains("output_folder"));
    REQUIRE_FALSE(fetch.params.contains("private_folder"));
    REQUIRE(fetch.steps.size() == 3);

    fetch.params["private_folder"] = "C:/Users/Ann/AppData/Local/Plynth/fetch";
    fetch.params["output_folder"] = "C:/Users/Ann/AppData/Local/Plynth/fetch/out";
    const Json job = makeBlenderJob(fetch.task, "blenderkit-1a2b3c4d", std::filesystem::path("D:/work/result.json"), fetch.params);
    REQUIRE(plynth::test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump())).empty());

    const ImportFetch material = makeBlenderKitFetch(kId, kFetchKindMaterial, BlenderKitOptions{});
    REQUIRE(material.expectedKind == "material");
}
