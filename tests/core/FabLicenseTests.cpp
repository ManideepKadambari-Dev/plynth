#include "StoreTestRig.h"

#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Integrations.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace plynth;
using plynth::test::AssetRig;
using plynth::test::MaterialRig;

namespace {

using Names = std::vector<std::string>;

const Names kVariants{"Chair/FBX/Low/Chair_LOD0.fbx", "Chair/FBX/Medium/Chair_LOD0.fbx", "Chair/Chair.json"};
const Names kFlat{"Plain_Oak_Stool_03_zqxwvut_High.fbx", "Plain_Oak_Stool_03_zqxwvut_High_4K_BaseColor.jpg", "zqxwvut.json"};
const Names kSizes{"Oak_2K_BaseColor.png", "Oak_4K_BaseColor.png"};
const Names kFlatMaterial{"Plain_Oak_Boards_zqxwvut_High_4K_BaseColor.jpg", "zqxwvut.json"};

// One way to import, and the license it has to record.
struct Way {
    const char* name;
    bool byHand;
    const char* saved;
    const char* option;
    const char* requested;
    const char* expected;
};

const std::vector<Way> kWays{
    {"guessed", false, "", "", "", "unknown"},
    {"guessed, saved cc0", false, "cc0", "", "", "unknown"},
    {"guessed, saved royalty free", false, "royalty_free", "", "", "unknown"},
    {"by hand", true, "", "", "", "royalty_free"},
    {"by hand, saved cc0", true, "cc0", "", "", "cc0"},
    {"guessed, option cc0", false, "", "cc0", "", "cc0"},
    {"by hand, option over saved", true, "cc0", "cc_by_4", "", "cc_by_4"},
    {"guessed, requested", false, "cc0", "", "cc_by_4", "cc_by_4"},
    {"by hand, requested", true, "cc0", "", "cc_by_4", "cc_by_4"},
    {"guessed, requested over option", false, "", "cc0", "cc_by_4", "cc_by_4"}};

// Imports a zip one way and checks the dry run and what was recorded.
template <class Rig, class Request, class Preview, class Import>
void checkWay(const Way& way, const Names& names, Preview preview, Import import)
{
    INFO(way.name << ", " << names.front());
    Rig rig("li");
    if (*way.saved) plynth::test::requireOk(setIntegrationOptions(rig.lib.paths, kFabIntegrationId, Json{{"license", way.saved}}));
    Request request;
    request.source.file = rig.zip("item.zip", names);
    if (way.byHand) request.importer = std::string(kFabImporterId);
    if (*way.option) request.importerOptions = Json{{"license", way.option}};

    const ImportPreview shown = preview(rig.lib.paths, request);
    plynth::test::requireOk(shown.status);
    CHECK(shown.importer == kFabImporterId);
    if (!*way.requested) CHECK(shown.plan.value("license", std::string()) == way.expected);

    request.license = way.requested;
    const std::string id = import(rig, request);
    const nlohmann::json info = rig.info(id);
    CHECK(info.at("license") == way.expected);
    CHECK(info.at("source").at("kind") == "fab_zip");
}

}

TEST_CASE("a Fab asset records the license of how it was chosen", "[store_import][fab_license]")
{
    for (const Names& names : {kVariants, kFlat}) {
        for (const Way& way : kWays) {
            checkWay<AssetRig, ImportRequest>(way, names, previewAssetImport, [](AssetRig& rig, const ImportRequest& request) {
                const ImportOutcome outcome = importAsset(*rig.lib.catalog, rig.context, request);
                plynth::test::requireOk(outcome.status);
                return outcome.assetId;
            });
        }
    }
}

TEST_CASE("a Fab material records the license of how it was chosen", "[store_import][fab_license]")
{
    for (const Names& names : {kSizes, kFlatMaterial}) {
        for (const Way& way : kWays) {
            checkWay<MaterialRig, MaterialImportRequest>(
                way, names, previewMaterialImport, [](MaterialRig& rig, const MaterialImportRequest& request) {
                    const MaterialImportOutcome outcome = importMaterial(*rig.lib.catalog, rig.context, request);
                    plynth::test::requireOk(outcome.status);
                    return outcome.materialId;
                });
        }
    }
}

TEST_CASE("both inboxes record unknown for a zip that looks like Fab, whatever is saved", "[store_import][fab_license]")
{
    for (const char* saved : {"", "cc0"}) {
        for (const Names& names : {kVariants, kFlat}) {
            INFO(names.front() << ", saved " << saved);
            AssetRig rig("la");
            if (*saved) plynth::test::requireOk(setIntegrationOptions(rig.lib.paths, kFabIntegrationId, Json{{"license", saved}}));
            const ImportOutcome outcome = importInboxEntry(*rig.lib.catalog, rig.context, rig.inboxZip("item.zip", names));
            plynth::test::requireOk(outcome.status);
            CHECK(outcome.importer == kFabImporterId);
            CHECK(rig.info(outcome.assetId)["license"] == "unknown");
        }
        for (const Names& names : {kSizes, kFlatMaterial}) {
            INFO(names.front() << ", saved " << saved);
            MaterialRig rig("lm");
            if (*saved) plynth::test::requireOk(setIntegrationOptions(rig.lib.paths, kFabIntegrationId, Json{{"license", saved}}));
            const MaterialImportOutcome outcome =
                importMaterialInboxEntry(*rig.lib.catalog, rig.context, rig.inboxZip("item.zip", names));
            plynth::test::requireOk(outcome.status);
            CHECK(outcome.importer == kFabImporterId);
            CHECK(rig.info(outcome.materialId)["license"] == "unknown");
        }
    }
}

TEST_CASE("picking the model of a guessed zip stays a guess", "[store_import][fab_license]")
{
    AssetRig rig("lp");
    plynth::test::requireOk(setIntegrationOptions(rig.lib.paths, kFabIntegrationId, Json{{"license", "cc0"}}));
    ImportRequest request;
    request.source.file = rig.zip("pack.zip", {"Chair/Chair_High.fbx", "Chair/Chair_Low.fbx", "Table/Table_High.fbx"});
    const ImportOutcome refused = importAsset(*rig.lib.catalog, rig.context, request);
    REQUIRE(refused.status.code == ErrorCode::InvalidValue);
    request.importerOptions = Json{{"model", "Table/Table_High.fbx"}};
    const ImportOutcome picked = importAsset(*rig.lib.catalog, rig.context, request);
    plynth::test::requireOk(picked.status);
    CHECK(rig.info(picked.assetId)["license"] == "unknown");
}
