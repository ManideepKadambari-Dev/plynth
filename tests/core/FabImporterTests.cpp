#include <plynth/core/catalog/AssetInfo.h>
#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/Integrations.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include "SchemaSupport.h"
#include "TestSupport.h"
#include "ZipTestWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace plynth;

namespace {

// Writes a zip of tiny files into the folder and gives the import source for it.
ImportSource zipSource(const test::TempDir& tmp, const std::string& name, std::initializer_list<const char*> entries,
                       Json options = Json::object())
{
    ImportSource source;
    source.file = tmp.path() / name;
    test::writeZipFile(source.file, test::zipEntriesNamed(entries));
    source.options = std::move(options);
    return source;
}

const std::initializer_list<const char*> kVariants = {
    "Chair/FBX/Low/Chair_LOD0.fbx",
    "Chair/FBX/Medium/Chair_LOD0.fbx",
    "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png",
    "Chair/FBX/Medium/Textures/T_Chair_Normal_2K.png",
    "Chair/FBX/High/Chair_LOD0.fbx",
    "Chair/FBX/High/Textures/T_Chair_BaseColor_2K.png",
    "Chair/Previews/front.png",
    "Chair/Chair.json",
};

const std::initializer_list<const char*> kFlat = {
    "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High.fbx",
    "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_Normal.jpg",
    "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_BaseColor.jpg",
    "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_Roughness.jpg",
    "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_AO.jpg",
    "abcdefg.json",
};

}

TEST_CASE("the fab importer scores and is picked", "[fab_importer]")
{
    test::TempDir tmp("fabimp-pick");
    const auto variants = zipSource(tmp, "variants.zip", kVariants);
    const auto* fab = findImporter(kFabImporterId);
    REQUIRE(fab);
    CHECK(fab->score(variants) == 20);
    REQUIRE(pickImporter(variants));
    CHECK(pickImporter(variants)->id() == kFabImporterId);

    const auto flat = zipSource(tmp, "flat_high.zip", kFlat);
    CHECK(fab->score(flat) == 20);
    CHECK(pickImporter(flat)->id() == kFabImporterId);

    const auto plain = zipSource(tmp, "plain.zip", {"thing.obj", "thing.mtl"});
    CHECK(fab->score(plain) == 5);
    CHECK(pickImporter(plain)->id() == "local_file");

    const auto uasset = zipSource(tmp, "unreal.zip", {"Chair/Chair.uasset"});
    CHECK(fab->score(uasset) == 20);
    CHECK(pickImporter(uasset)->id() == kFabImporterId);

    ImportSource broken;
    broken.file = tmp.path() / "broken.zip";
    std::ofstream(broken.file, std::ios::binary | std::ios::trunc) << "not a zip";
    CHECK(fab->score(broken) == 0);
    ImportSource obj;
    obj.file = tmp.path() / "a.obj";
    CHECK(fab->score(obj) == 0);
    ImportSource address;
    address.url = "https://www.fab.com/";
    CHECK(fab->score(address) == 0);
}

TEST_CASE("the fab importer plans a variants zip", "[fab_importer]")
{
    test::TempDir tmp("fabimp-plan");
    const auto work = tmp.path() / "work";
    std::filesystem::create_directories(work);
    auto source = zipSource(tmp, "Chair.zip", kVariants, Json{{"license", "cc0"}});
    ImportPlan plan;
    test::requireOk(findImporter(kFabImporterId)->plan(source, work, plan));

    CHECK(plan.format == kFormatFab);
    CHECK(plan.sourceKind == kSourceFabZip);
    CHECK(plan.reference == "Chair.zip");
    CHECK(plan.defaultName == "Chair");
    CHECK(plan.license == "cc0");
    CHECK(plan.originalIsZip);
    CHECK(plan.file == work / std::string(kFabSourceFileName));
    CHECK(plan.details["picked"] == "Chair/FBX/Medium/Chair_LOD0.fbx");
    CHECK(plan.details.contains("models"));
    CHECK(plan.details.contains("textures"));
    CHECK(plan.details.contains("skipped"));

    const auto json = Json::parse(test::readTextFile(plan.file));
    CHECK(test::schemaProblem("fab-source.schema.json", nlohmann::json::parse(json.dump())).empty());
    CHECK(json["kind"] == "model");
    CHECK(json["model"] == "Chair/FBX/Medium/Chair_LOD0.fbx");

    ImportPlan high;
    source.options = Json{{"quality", "high"}};
    test::requireOk(findImporter(kFabImporterId)->plan(source, work, high));
    CHECK(high.details["picked"] == "Chair/FBX/High/Chair_LOD0.fbx");
}

TEST_CASE("the fab importer plans the flat Megascans layout", "[fab_importer]")
{
    test::TempDir tmp("fabimp-flat");
    const auto work = tmp.path() / "work";
    std::filesystem::create_directories(work);
    const auto source = zipSource(tmp, "imagined_cottage_equipment_stool_plain_tan_03_abcdefg_high.zip", kFlat);
    ImportPlan plan;
    test::requireOk(findImporter(kFabImporterId)->plan(source, work, plan));
    CHECK(plan.defaultName == "Imagined Cottage Equipment Stool Plain Tan 03");
    CHECK(plan.details["picked"] == "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High.fbx");
    CHECK(plan.details["textures"].size() == 4);
    CHECK(plan.details["metadata"] == "abcdefg.json");
    CHECK(plan.details["texture_resolution"].is_null());
    const auto json = Json::parse(test::readTextFile(plan.file));
    CHECK(json["files"].size() == 6);
    CHECK(test::schemaProblem("fab-source.schema.json", nlohmann::json::parse(json.dump())).empty());
}

TEST_CASE("the fab importer refuses with plain sentences", "[fab_importer]")
{
    test::TempDir tmp("fabimp-refuse");
    const auto work = tmp.path() / "work";
    std::filesystem::create_directories(work);
    const auto* fab = findImporter(kFabImporterId);
    ImportPlan plan;

    auto status = fab->plan(zipSource(tmp, "u.zip", {"Chair/Chair.uasset"}), work, plan);
    CHECK(status.code == ErrorCode::InvalidValue);
    CHECK(status.message == "This zip holds no model Plynth can read (found: .uasset). Download the FBX or glTF version from Fab.");

    status = fab->plan(zipSource(tmp, "many.zip", {"Chair.fbx", "Table.fbx"}), work, plan);
    CHECK(status.code == ErrorCode::InvalidValue);
    CHECK(status.message == "This zip holds several models: Chair, Table. Pick one with the option model.");

    status = fab->plan(zipSource(tmp, "m.zip", kVariants, Json{{"model", "Nope.fbx"}}), work, plan);
    CHECK(status.message == "The zip has no model Nope.fbx.");

    status = fab->plan(zipSource(tmp, "o.zip", kVariants, Json{{"quality", "ultra"}}), work, plan);
    CHECK(status.code == ErrorCode::InvalidValue);

    ImportSource missing;
    missing.file = tmp.path() / "missing.zip";
    CHECK(fab->plan(missing, work, plan).code == ErrorCode::NotFound);
}

TEST_CASE("the fab material importer wins for zips with several resolutions", "[fab_importer]")
{
    test::TempDir tmp("fabimp-material");
    const auto* fab = findMaterialImporter(kFabImporterId);
    REQUIRE(fab);
    const auto sets = zipSource(tmp, "Oak.zip",
                                {"Oak/Oak_BaseColor_2K.png", "Oak/Oak_BaseColor_4K.png", "Oak/Oak_Normal_2K.png",
                                 "Oak/Oak_Normal_4K.png", "Oak/Preview.png", "Oak/Oak.json"});
    CHECK(fab->score(sets) == 20);
    REQUIRE(pickMaterialImporter(sets));
    CHECK(pickMaterialImporter(sets)->id() == kFabImporterId);

    const auto plain = zipSource(tmp, "plain.zip", {"Brick_BaseColor.png", "Brick_Normal.png"});
    CHECK(fab->score(plain) == 5);
    CHECK(pickMaterialImporter(plain)->id() == "texture_set");

    CHECK(fab->score(zipSource(tmp, "model.zip", kVariants)) == 0);

    const auto work = tmp.path() / "work";
    std::filesystem::create_directories(work);
    MaterialImportPlan plan;
    const auto source = zipSource(tmp, "Muddy_Cloth_abcdefghi_High.zip", {"Muddy_Cloth_abcdefghi_High_4K_BaseColor.jpg", "abcdefghi.json"});
    test::requireOk(fab->plan(source, work, plan));
    CHECK(plan.workerSource == kMaterialSourceFab);
    CHECK(plan.sourceKind == kSourceFabZip);
    CHECK(plan.path == work / std::string(kFabSourceFileName));
    CHECK(plan.defaultName == "Muddy Cloth");
    CHECK(plan.license == "royalty_free");
    CHECK(plan.originalIsZip);
    const auto json = Json::parse(test::readTextFile(plan.path));
    CHECK(json["kind"] == "material");
    CHECK(test::schemaProblem("fab-source.schema.json", nlohmann::json::parse(json.dump())).empty());

    const auto status = fab->plan(zipSource(tmp, "none.zip", {"readme.txt"}), work, plan);
    CHECK(status.message == "This zip holds no texture images.");
}

TEST_CASE("the fab integration lists both importers and checks options", "[fab_importer]")
{
    const auto* fab = findIntegration(kFabIntegrationId);
    REQUIRE(fab);
    CHECK(fab->order() == 30);
    CHECK(fab->assetImporters() == std::vector<std::string>{"fab_zip"});
    CHECK(fab->materialImporters() == std::vector<std::string>{"fab_zip"});
    CHECK_FALSE(fab->takesAddresses());
    CHECK(fab->setupItem().empty());
    CHECK(fab->site() == kFabSite);
    CHECK(fab->defaultOptions() == fabOptionsToJson(FabOptions{}));
    CHECK(integrationForImporter("fab_zip", false) == fab);
    CHECK(integrationForImporter("fab_zip", true) == fab);

    CHECK(fab->checkOptions(Json{{"quality", "high"}}, OptionUse::Saved).ok());
    CHECK(fab->checkOptions(Json{{"model", "a.fbx"}}, OptionUse::Import).ok());
    CHECK(fab->checkOptions(Json{{"model", "a.fbx"}}, OptionUse::Saved).code == ErrorCode::InvalidValue);
    CHECK(fab->checkOptions(Json{{"nope", 1}}, OptionUse::Saved).code == ErrorCode::InvalidValue);
}
