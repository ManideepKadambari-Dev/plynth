#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Integrations.h>
#include <plynth/core/catalog/MaterialImport.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// The text of an integrations file with the given format and version, written as JSON.
std::string fileText(const std::string& format, const std::string& version)
{
    return "{\"format\":" + format + ",\"version\":" + version + ",\"integrations\":{}}";
}

}

TEST_CASE("an integrations format that is not text is a bad file everywhere", "[core][integrations][store_safety]")
{
    for (const char* format : {"1", "null", "[]", "{}", "true", "1.5"}) {
        INFO(format);
        plynth::test::IsolatedUser user("if");
        plynth::test::TestLibrary lib("if");
        const std::string text = fileText(format, "1");
        plynth::test::writeBytes(integrationsFile(lib.paths), text);

        IntegrationOptionsRead read;
        CHECK_NOTHROW(read = readIntegrationOptions(lib.paths));
        CHECK(read.status.code == ErrorCode::BadFile);
        CHECK(read.status.where == "/format");

        Status set;
        CHECK_NOTHROW(set = setIntegrationOptions(lib.paths, kFabIntegrationId, Json{{"quality", "high"}}));
        CHECK(set.code == ErrorCode::BadFile);
        CHECK(set.where == "/format");
        Status reset;
        CHECK_NOTHROW(reset = resetIntegrationOptions(lib.paths, kFabIntegrationId));
        CHECK(reset.code == ErrorCode::BadFile);
        CHECK(reset.where == "/format");

        ImportRequest request;
        request.source.url = "asset_base_id:0c1d4a2e-1111-2222-3333-444455556666";
        ImportPreview preview;
        CHECK_NOTHROW(preview = previewAssetImport(lib.paths, request));
        CHECK(preview.status.code == ErrorCode::BadFile);

        ImportContext context;
        context.library = lib.paths;
        context.blender = inspectBlender(plynth::test::makeFakeBlender(lib.tmp.path() / "b", "5.2.0"));
        context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "s");
        ImportOutcome imported;
        CHECK_NOTHROW(imported = importAsset(*lib.catalog, context, request));
        CHECK(imported.status.code == ErrorCode::BadFile);

        const fs::path entry = lib.paths.assetInbox / "chair.obj";
        plynth::test::writeBytes(entry, "v 0 0 0\n");
        ImportOutcome inbox;
        CHECK_NOTHROW(inbox = importInboxEntry(*lib.catalog, context, entry));
        CHECK(inbox.status.code == ErrorCode::BadFile);
        CHECK(fs::exists(entry));
        CHECK(inbox.movedTo.empty());

        Status opened;
        const auto materials = MaterialCatalog::open(lib.paths, opened);
        plynth::test::requireOk(opened);
        const fs::path material = lib.paths.materialInbox / "Oak_BaseColor.png";
        plynth::test::writeBytes(material, "x");
        MaterialImportOutcome materialInbox;
        CHECK_NOTHROW(materialInbox = importMaterialInboxEntry(*materials, context, material));
        CHECK(materialInbox.status.code == ErrorCode::BadFile);
        CHECK(fs::exists(material));
        CHECK(materialInbox.movedTo.empty());

        CHECK(plynth::test::readTextFile(integrationsFile(lib.paths)) == text);
        CHECK_FALSE(fs::exists(lib.tmp.path() / "b" / "fake_jobs.log"));
    }
}

TEST_CASE("an integrations version out of range is refused", "[core][integrations][store_safety]")
{
    plynth::test::TestLibrary lib("iv");
    for (const char* version : {"2", "4294967297", "9223372036854775807", "18446744073709551615"}) {
        INFO(version);
        plynth::test::writeBytes(integrationsFile(lib.paths), fileText("\"plynth-integrations\"", version));
        CHECK(readIntegrationOptions(lib.paths).status.code == ErrorCode::UnsupportedVersion);
        CHECK(setIntegrationOptions(lib.paths, kFabIntegrationId, Json{{"quality", "high"}}).code == ErrorCode::UnsupportedVersion);
    }
    for (const char* version : {"0", "-1", "-4294967295"}) {
        INFO(version);
        plynth::test::writeBytes(integrationsFile(lib.paths), fileText("\"plynth-integrations\"", version));
        const IntegrationOptionsRead read = readIntegrationOptions(lib.paths);
        CHECK(read.status.code == ErrorCode::BadFile);
        CHECK(read.status.where == "/version");
    }
}

TEST_CASE("a license that breaks the schema is not saved", "[core][integrations][store_safety]")
{
    for (const std::string& license : {std::string("Royalty Free"), std::string("../x"), std::string(65, 'a'), std::string("CC0"),
                                       std::string("1free"), std::string("_free"), std::string("cc0\n")}) {
        INFO(license);
        plynth::test::TestLibrary lib("il");
        CHECK(setIntegrationOptions(lib.paths, kFabIntegrationId, Json{{"license", license}}).code == ErrorCode::InvalidValue);
        CHECK_FALSE(fs::exists(integrationsFile(lib.paths)));
    }
    for (const std::string& license : {std::string("cc0"), std::string("royalty_free"), std::string(64, 'a')}) {
        INFO(license);
        plynth::test::TestLibrary lib("ig");
        plynth::test::requireOk(setIntegrationOptions(lib.paths, kFabIntegrationId, Json{{"license", license}}));
        const auto written = nlohmann::json::parse(plynth::test::readTextFile(integrationsFile(lib.paths)));
        CHECK(plynth::test::schemaProblem("integrations.schema.json", written).empty());
    }
}

TEST_CASE("an integrations entry that breaks the schema is a bad file", "[core][integrations][store_safety]")
{
    const std::pair<const char*, const char*> cases[] = {
        {"{\"Bad-Id\":{}}", "/integrations/Bad-Id"}, {"{\"other\":5}", "/integrations/other"}, {"{\"fab\":[]}", "/integrations/fab"}};
    for (const auto& [entries, where] : cases) {
        INFO(entries);
        plynth::test::TestLibrary lib("ie");
        plynth::test::writeBytes(integrationsFile(lib.paths),
                                 std::string("{\"format\":\"plynth-integrations\",\"version\":1,\"integrations\":") + entries + "}");
        const IntegrationOptionsRead read = readIntegrationOptions(lib.paths);
        CHECK(read.status.code == ErrorCode::BadFile);
        CHECK(read.status.where == where);
        CHECK(setIntegrationOptions(lib.paths, kFabIntegrationId, Json{{"quality", "high"}}).code == ErrorCode::BadFile);
    }
}
