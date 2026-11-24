#include "StoreTestRig.h"

#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/Integrations.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using plynth::test::AssetRig;
using plynth::test::MaterialRig;
namespace fs = std::filesystem;

namespace {

constexpr const char* kKind = "test_store_item";
constexpr const char* kWhich = "Tell Plynth which piece you want.";

// The models a test plan offers.
Json pieces()
{
    return Json{{"models", Json::array({Json{{"entry", "left"}}, Json{{"entry", "right"}}})}};
}

// A file importer of the test store. Only files named teststore_ get a score.
class TestStoreImporter final : public Importer {
public:
    // Keeps the id.
    explicit TestStoreImporter(std::string id) : id_(std::move(id)) {}
    std::string_view id() const override { return id_; }
    std::string_view name() const override { return "Test store"; }
    std::string_view description() const override { return "Test importer."; }
    std::vector<std::string> fileSuffixes() const override { return {}; }
    // Five, so Own files wins when nothing names this importer.
    int score(const ImportSource& source) const override
    {
        return pathToUtf8(source.file.filename()).starts_with("teststore_") ? 5 : 0;
    }
    // Refuses a pair in its own words until a model is picked.
    Status plan(const ImportSource& source, const fs::path&, ImportPlan& out) const override
    {
        out = {};
        out.details = pieces();
        const bool picked = source.options.is_object() && source.options.contains("model");
        if (!picked && pathToUtf8(source.file.filename()).find("pair") != std::string::npos) {
            return fail(ErrorCode::InvalidValue, kWhich);
        }
        out.file = source.file;
        out.format = "obj";
        out.sourceKind = kKind;
        out.reference = "test";
        return {};
    }

private:
    std::string id_;
};

// The same for materials.
class TestStoreMaterialImporter final : public MaterialImporter {
public:
    std::string_view id() const override { return "test_store_material"; }
    std::string_view name() const override { return "Test store"; }
    std::string_view description() const override { return "Test importer."; }
    std::vector<std::string> fileSuffixes() const override { return {}; }
    // Five, as for models.
    int score(const ImportSource& source) const override
    {
        return pathToUtf8(source.file.filename()).starts_with("teststore_") ? 5 : 0;
    }
    // Refuses a pair in its own words.
    Status plan(const ImportSource& source, const fs::path&, MaterialImportPlan& out) const override
    {
        out = {};
        out.details = pieces();
        if (pathToUtf8(source.file.filename()).find("pair") != std::string::npos) return fail(ErrorCode::InvalidValue, kWhich);
        out.workerSource = "texture_set";
        out.path = source.file;
        out.sourceKind = kKind;
        out.reference = "test";
        return {};
    }
};

// The integration of the test store: everything it needs is in this file.
class TestStore final : public Integration {
public:
    std::string_view id() const override { return "test_store"; }
    std::string_view name() const override { return "Test store"; }
    std::string_view description() const override { return "Test source."; }
    int order() const override { return 900; }
    std::vector<std::string> assetImporters() const override { return {"test_store_b", "test_store_a"}; }
    std::vector<std::string> materialImporters() const override { return {"test_store_material"}; }
    bool takesAddresses() const override { return false; }
    std::string_view setupItem() const override { return {}; }
    std::string_view site() const override { return {}; }
    Json defaultOptions() const override { return Json::object(); }
    // Takes a picked model for one import.
    Status checkOptions(const Json& options, OptionUse use) const override
    {
        if (options.empty() || (use == OptionUse::Import && options.size() == 1 && options.contains("model"))) return {};
        return fail(ErrorCode::InvalidValue, "No such option.");
    }
    std::vector<std::string> storeSourceKinds() const override { return {kKind}; }
};

// Puts the test store in the registries once.
const Integration& testStore()
{
    static const bool added = [] {
        registerImporter(std::make_unique<TestStoreImporter>("test_store_b"));
        registerImporter(std::make_unique<TestStoreImporter>("test_store_a"));
        registerMaterialImporter(std::make_unique<TestStoreMaterialImporter>());
        return registerIntegration(std::make_unique<TestStore>());
    }();
    (void)added;
    return *findIntegration("test_store");
}

// The field of a key, or one with an empty key.
OptionField fieldOf(std::string_view integration, const std::string& key)
{
    for (const OptionField& field : findIntegration(integration)->optionFields()) {
        if (field.key == key) return field;
    }
    return {};
}

// The values of a field's choices.
std::vector<Json> valuesOf(const OptionField& field)
{
    std::vector<Json> out;
    for (const OptionChoice& choice : field.choices) out.push_back(choice.value);
    return out;
}

}

TEST_CASE("a new integration brings its store kind without a change of core", "[core][integrations][store_source]")
{
    testStore();
    CHECK(isStoreSource(kKind));
    CHECK(isStoreSource("blenderkit"));
    CHECK(isStoreSource("fab_zip"));
    CHECK_FALSE(isStoreSource("local_file"));
    CHECK_FALSE(isStoreSource("texture_set"));
    CHECK(findIntegration("fab")->storeSourceKinds() == std::vector<std::string>{"fab_zip"});
    CHECK(findIntegration("blenderkit")->storeSourceKinds() == std::vector<std::string>{"blenderkit"});
    CHECK(findIntegration("local")->storeSourceKinds().empty());
}

TEST_CASE("an integration's button names that integration's importer", "[core][integrations][store_source]")
{
    const Integration& store = testStore();
    ImportSource file;
    file.file = "teststore_chair.obj";
    CHECK(integrationImporter(store, file, false) == "test_store_a");
    CHECK(integrationImporter(store, file, true) == "test_store_material");
    CHECK(integrationImporter(*findIntegration("local"), file, false) == "local_file");
    ImportSource other;
    other.file = "chair.obj";
    CHECK(integrationImporter(store, other, false).empty());

    ImportSource address;
    address.url = "asset_base_id:0c1d4a2e-1111-2222-3333-444455556666";
    CHECK(integrationImporter(*findIntegration("blenderkit"), address, false) == "blenderkit");
    CHECK(integrationImporter(*findIntegration("blenderkit"), address, true) == "blenderkit");
    CHECK(integrationImporter(*findIntegration("fab"), address, false).empty());
}

TEST_CASE("an item of a new source keeps that source", "[store_import][store_source]")
{
    const Integration& store = testStore();
    AssetRig rig("ss");
    ImportRequest request;
    request.source.file = rig.lib.tmp.path() / "in" / "teststore_chair.obj";
    plynth::test::writeBytes(request.source.file, "v 0 0 0\n");
    request.importer = integrationImporter(store, request.source, false);
    const ImportOutcome outcome = importAsset(*rig.lib.catalog, rig.context, request);
    plynth::test::requireOk(outcome.status);
    CHECK(outcome.importer == "test_store_a");
    CHECK(rig.info(outcome.assetId)["source"]["kind"] == kKind);
    CHECK(outcome.details == pieces());
}

TEST_CASE("a refusal in any words still offers its models", "[store_import][store_source]")
{
    testStore();
    AssetRig rig("sp");
    ImportRequest request;
    request.importer = "test_store_a";
    request.source.file = rig.lib.tmp.path() / "in" / "teststore_pair.obj";
    plynth::test::writeBytes(request.source.file, "v 0 0 0\n");
    const ImportOutcome refused = importAsset(*rig.lib.catalog, rig.context, request);
    CHECK(refused.status.code == ErrorCode::InvalidValue);
    CHECK(refused.status.message == kWhich);
    CHECK(refused.details == pieces());
    CHECK(rig.jobs().empty());
    request.importerOptions = Json{{"model", "right"}};
    CHECK(importAsset(*rig.lib.catalog, rig.context, request).status.ok());

    MaterialRig materials("sm");
    MaterialImportRequest material;
    material.importer = "test_store_material";
    material.source.file = materials.lib.tmp.path() / "in" / "teststore_pair.png";
    plynth::test::writeBytes(material.source.file, "x");
    const MaterialImportOutcome no = importMaterial(*materials.lib.catalog, materials.context, material);
    CHECK(no.status.code == ErrorCode::InvalidValue);
    CHECK(no.details == pieces());
}

TEST_CASE("BlenderKit checks an address for whoever shows the field", "[core][integrations][store_source]")
{
    const Integration* kit = findIntegration("blenderkit");
    CHECK(kit->checkAddress("asset_base_id:0c1d4a2e-1111-2222-3333-444455556666").ok());
    for (const char* text : {"not an address", "https://example.org/item", "asset_base_id:123"}) {
        INFO(text);
        const Status status = kit->checkAddress(text);
        CHECK(status.code == ErrorCode::InvalidValue);
        CHECK(status.message == parseBlenderKitAddress(text).status.message);
    }
}

TEST_CASE("the app has a field for every option the CLI can save", "[core][integrations][option_fields]")
{
    for (const char* id : {"local", "blenderkit", "fab"}) {
        INFO(id);
        const Integration* integration = findIntegration(id);
        std::vector<std::string> defaults;
        const Json options = integration->defaultOptions();
        for (const auto& item : options.items()) defaults.push_back(item.key());
        std::vector<std::string> fields;
        for (const OptionField& field : integration->optionFields()) {
            fields.push_back(field.key);
            CHECK_FALSE(field.label.empty());
            CHECK_FALSE(field.help.empty());
            for (const OptionChoice& choice : field.choices) {
                CHECK_FALSE(choice.label.empty());
                CHECK(integration->checkOptions(Json{{field.key, choice.value}}, OptionUse::Saved).ok());
            }
        }
        CHECK(fields == defaults);
    }
}

TEST_CASE("the fields hold what each option allows", "[core][integrations][option_fields]")
{
    const OptionField wait = fieldOf("blenderkit", "wait_seconds");
    CHECK(wait.kind == OptionKind::WholeNumber);
    CHECK(wait.minimum == 60);
    CHECK(wait.maximum == 3600);

    const OptionField resolution = fieldOf("blenderkit", "resolution");
    CHECK(resolution.kind == OptionKind::Choice);
    CHECK(valuesOf(resolution) == std::vector<Json>{"0.5k", "1k", "2k", "4k", "8k", "original"});

    CHECK(valuesOf(fieldOf("fab", "formats")) ==
          std::vector<Json>{Json::array({"fbx", "gltf", "obj", "blend"}), Json::array({"gltf", "fbx", "obj", "blend"}),
                            Json::array({"obj", "fbx", "gltf", "blend"}), Json::array({"blend", "fbx", "gltf", "obj"})});
    CHECK(valuesOf(fieldOf("fab", "quality")) == std::vector<Json>{"low", "medium", "high", "raw"});
    CHECK(valuesOf(fieldOf("fab", "normal")) == std::vector<Json>{"auto", "opengl", "directx"});
    CHECK(valuesOf(fieldOf("fab", "max_texture_size")) == std::vector<Json>{512, 1024, 2048, 4096, 8192, 16384, nullptr});
    CHECK(fieldOf("fab", "license").kind == OptionKind::License);
}

TEST_CASE("a saved wait reaches the dry run", "[preview][option_fields]")
{
    AssetRig rig("sw");
    plynth::test::requireOk(setIntegrationOptions(rig.lib.paths, "blenderkit", Json{{"wait_seconds", 900}}));
    ImportRequest request;
    request.source.url = "asset_base_id:0c1d4a2e-1111-2222-3333-444455556666";
    const ImportPreview preview = previewAssetImport(rig.lib.paths, request);
    plynth::test::requireOk(preview.status);
    CHECK(preview.options.at("wait_seconds") == 900);
    CHECK(preview.plan.at("fetch").at("params").at("wait_seconds") == 900);
}

TEST_CASE("a pick for one material reaches the worker with that name", "[store_import][material_picks]")
{
    AssetRig rig("sn", "fail");
    ImportRequest request;
    request.importer = std::string(kFabImporterId);
    request.source.file = rig.zip("two.zip", {"Thing.fbx", "Wood_BaseColor.png", "Metal_BaseColor.png"});
    request.importerOptions = Json{{"maps", Json{{"Wood", Json{{"base_color", "Metal_BaseColor.png"}}}}}};
    const ImportOutcome outcome = importAsset(*rig.lib.catalog, rig.context, request);
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    const auto source = nlohmann::json::parse(plynth::test::readTextFile(outcome.keptWorkFolder / "source.fab.json"));
    CHECK(source.at("maps") == nlohmann::json{{"Wood", {{"base_color", "Metal_BaseColor.png"}}}});
}
