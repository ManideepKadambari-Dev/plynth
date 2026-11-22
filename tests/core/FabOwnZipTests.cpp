#include "StoreTestRig.h"

#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace plynth;
using plynth::test::AssetRig;
using plynth::test::MaterialRig;
namespace fs = std::filesystem;

namespace {

using Names = std::vector<std::string>;

// The material.json of the user guide, with every field that only texture_set keeps.
const char* const kMaterialJson =
    R"({"id":"old_brick","name":"Old brick","groups":["brick"],"tags":["red"],"tile_size":{"width":1.2,"height":0.6},)"
    R"("values":{"roughness":0.8},"license":"cc0"})";

// Writes a texture zip that holds the user's material.json.
fs::path ownTextureZip(const fs::path& folder, const Names& images, const std::string& sidecar)
{
    fs::create_directories(folder);
    std::vector<plynth::test::ZipTestEntry> entries{{sidecar, kMaterialJson}};
    for (const std::string& image : images) entries.push_back({image, "x"});
    const fs::path file = folder / "brick.zip";
    plynth::test::writeZipFile(file, entries);
    return file;
}

// Texture zips that are the user's own.
struct OwnSet {
    Names images;
    std::string sidecar;
};

const std::vector<OwnSet> kOwnSets{
    {{"Brick_BaseColor.png", "Brick_Normal.png"}, "material.json"},
    {{"Brick/Brick_BaseColor.png", "Brick/Brick_Normal.png"}, "Brick/material.json"},
    {{"Brick_BaseColor.png"}, "Material.JSON"},
    {{"Brick_2K_BaseColor.png", "Brick_4K_BaseColor.png"}, "material.json"},
    {{"Brick_2K_BaseColor.png", "Brick_4K_BaseColor.png", "extra.json"}, "material.json"}};

}

TEST_CASE("a zip with the user's material.json stays a texture set", "[store_import][own_zip]")
{
    for (const OwnSet& set : kOwnSets) {
        INFO(set.sidecar << ", " << set.images.front() << ", " << set.images.size());
        Names names = set.images;
        names.push_back(set.sidecar);
        CHECK_FALSE(looksLikeFabMaterialZip(plynth::test::fabContentsOf(names)));

        MaterialRig rig("om");
        MaterialImportRequest request;
        request.source.file = ownTextureZip(rig.lib.tmp.path() / "in", set.images, set.sidecar);
        const MaterialImporter* best = pickMaterialImporter(request.source);
        REQUIRE(best != nullptr);
        CHECK(best->id() == "texture_set");
        const ImportPreview preview = previewMaterialImport(rig.lib.paths, request);
        CHECK(preview.importer == "texture_set");
        CHECK(preview.plan.value("worker_source", std::string()) == "texture_set");
        request.keepOriginal = true;
        const MaterialImportOutcome outcome = importMaterial(*rig.lib.catalog, rig.context, request);
        plynth::test::requireOk(outcome.status);
        CHECK(outcome.importer == "texture_set");
        const auto jobs = rig.jobs();
        REQUIRE(jobs.size() == 1);
        // The worker gets the zip itself, so it reads every field of the file inside.
        CHECK(jobs[0].at("params").at("source").at("kind") == "texture_set");
        CHECK(fs::path(pathFromUtf8(jobs[0].at("params").at("source").at("path").get<std::string>())).filename() == "brick.zip");
        CHECK(rig.info(outcome.materialId)["source"]["kind"] == "texture_set");
        CHECK(rig.info(outcome.materialId)["license"] != "royalty_free");

        MaterialRig inbox("oi");
        const fs::path entry = ownTextureZip(inbox.lib.paths.materialInbox, set.images, set.sidecar);
        const MaterialImportOutcome taken = importMaterialInboxEntry(*inbox.lib.catalog, inbox.context, entry);
        plynth::test::requireOk(taken.status);
        CHECK(taken.importer == "texture_set");
    }
}

TEST_CASE("a folder named source or original does not make a zip Fab", "[store_import][own_zip]")
{
    for (const Names& names : {Names{"source/chair.fbx", "textures/chair.png"}, Names{"original/chair.obj"}, Names{"Source/chair.glb"}}) {
        INFO(names.front());
        CHECK_FALSE(looksLikeFabModelZip(plynth::test::fabContentsOf(names)));
        AssetRig rig("os");
        ImportRequest request;
        request.source.file = rig.zip("mine.zip", names);
        const ImportOutcome outcome = importAsset(*rig.lib.catalog, rig.context, request);
        plynth::test::requireOk(outcome.status);
        CHECK(outcome.importer == "local_file");
        CHECK(rig.info(outcome.assetId)["license"] == "unknown");
        CHECK(rig.info(outcome.assetId)["source"]["kind"] == "local_file");

        AssetRig inbox("ob");
        CHECK(importInboxEntry(*inbox.lib.catalog, inbox.context, inbox.inboxZip("mine.zip", names)).importer == "local_file");
    }
}

TEST_CASE("zips that look like Fab are still Fab", "[fab][own_zip]")
{
    for (const Names& names : {Names{"Plain_Oak_Stool_03_zqxwvut_High.fbx", "zqxwvut.json"},
                               Names{"Chair/FBX/Low/Chair.fbx", "Chair/FBX/Medium/Chair.fbx"}, Names{"Chair_LOD0.fbx"},
                               Names{"Source/Chair.fbx", "Original/Chair.fbx"}, Names{"FBX/Chair.fbx"}}) {
        INFO(names.front());
        CHECK(looksLikeFabModelZip(plynth::test::fabContentsOf(names)));
    }
    for (const Names& names : {Names{"Oak_2K_BaseColor.png", "Oak_4K_BaseColor.png"}, Names{"Oak_BaseColor.png", "zqxwvut.json"},
                               Names{"Oak_BaseColor.png", "my_material.json"}}) {
        INFO(names.front());
        CHECK(looksLikeFabMaterialZip(plynth::test::fabContentsOf(names)));
    }
}
