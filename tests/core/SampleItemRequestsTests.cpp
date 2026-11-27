#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"
#include "TestSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/MaterialCatalog.h>
#include <plynth/core/catalog/MaterialImport.h>
#include <plynth/core/catalog/SampleItems.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A small samples folder with one model and two materials (one copied from the real samples, one without a
// material.json), a library with both catalogs and a fake Blender.
struct RequestFixture {
    explicit RequestFixture(const std::string& tag) : lib(tag)
    {
        samples = lib.tmp.path() / "samples";
        const fs::path real = PLYNTH_SAMPLES_DIR;
        fs::create_directories(samples / "models");
        fs::create_directories(samples / "materials");
        fs::copy(real / "models" / "chair", samples / "models" / "chair", fs::copy_options::recursive);
        fs::copy(real / "materials" / "oak_planks", samples / "materials" / "oak_planks", fs::copy_options::recursive);
        plynth::test::writeBytes(samples / "materials" / "plain_set" / "base_col.png", "image");
        std::ofstream(samples / "samples.json", std::ios::binary)
            << R"({"format": "plynth-samples", "version": 1, "license": "cc0",
 "assets": [{"id": "chair", "name": "Chair", "file": "models/chair/chair.obj", "categories": ["chair"], "tags": ["wood"]}],
 "materials": [{"id": "oak_planks", "folder": "materials/oak_planks"}], "projects": []})";
        const fs::path exe = plynth::test::makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", "");
        context.library = lib.paths;
        context.blender = inspectBlender(exe);
        context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "scripts");
        context.removeOriginal = [](const fs::path&) { return Status{}; };
        Status status;
        materials = MaterialCatalog::open(lib.paths, status);
        plynth::test::requireOk(status);
    }

    plynth::test::TestLibrary lib;
    fs::path samples;
    ImportContext context;
    std::unique_ptr<MaterialCatalog> materials;
};

}

TEST_CASE("the sample item requests carry the license, the categories and keepOriginal", "[core][samples][licenses]")
{
    RequestFixture f("sample-requests");
    const SampleItemRequests requests = sampleItemRequests(*f.lib.catalog, *f.materials, f.samples);
    plynth::test::requireOk(requests.status);

    REQUIRE(requests.assets.size() == 1);
    const ImportRequest& chair = requests.assets[0];
    REQUIRE(chair.source.file == f.samples / "models" / "chair" / "chair.obj");
    REQUIRE(chair.id == "chair");
    REQUIRE(chair.name == "Chair");
    REQUIRE(chair.categories == std::vector<std::string>{"chair"});
    REQUIRE(chair.tags == std::vector<std::string>{"wood"});
    REQUIRE(chair.license == "cc0");
    REQUIRE(chair.keepOriginal == std::optional<bool>(true));

    // One request for each material folder, in name order, as sampleMaterialRequests gives them.
    REQUIRE(requests.materials.size() == 2);
    REQUIRE(requests.materials[0].source.file == f.samples / "materials" / "oak_planks");
    REQUIRE(requests.materials[1].source.file == f.samples / "materials" / "plain_set");
    for (const MaterialImportRequest& material : requests.materials) {
        REQUIRE(material.license == "cc0");
        REQUIRE(material.keepOriginal == std::optional<bool>(true));
        REQUIRE(material.skipExisting);
        REQUIRE(material.id.empty());
    }
}

TEST_CASE("the sample item requests leave out what the library has", "[core][samples]")
{
    RequestFixture f("sample-requests-have");
    f.lib.add("chair", "My chair", {"chair"});
    plynth::test::requireOk(f.materials->indexMaterial(plynth::test::makeMaterialOnDisk(f.lib.paths, "oak_planks", "My oak")));

    const SampleItemRequests requests = sampleItemRequests(*f.lib.catalog, *f.materials, f.samples);
    plynth::test::requireOk(requests.status);
    REQUIRE(requests.assets.empty());
    // The material without a material.json has no id to compare, so it stays and skipExisting decides.
    REQUIRE(requests.materials.size() == 1);
    REQUIRE(requests.materials[0].source.file == f.samples / "materials" / "plain_set");
}

TEST_CASE("the sample item requests without an index are not found and list nothing", "[core][samples]")
{
    RequestFixture f("sample-requests-none");
    fs::remove(f.samples / "samples.json");
    const SampleItemRequests requests = sampleItemRequests(*f.lib.catalog, *f.materials, f.samples);
    REQUIRE(requests.status.code == ErrorCode::NotFound);
    REQUIRE(requests.status.message.rfind("The samples were not found next to the program.", 0) == 0);
    REQUIRE(requests.assets.empty());
    REQUIRE(requests.materials.empty());
    REQUIRE(addSampleItems(*f.lib.catalog, *f.materials, f.context, f.samples).status.message == requests.status.message);
}

TEST_CASE("a samples folder without materials gives model requests only", "[core][samples]")
{
    RequestFixture f("sample-requests-nomat");
    fs::remove_all(f.samples / "materials");
    const SampleItemRequests requests = sampleItemRequests(*f.lib.catalog, *f.materials, f.samples);
    plynth::test::requireOk(requests.status);
    REQUIRE(requests.assets.size() == 1);
    REQUIRE(requests.materials.empty());
}

TEST_CASE("running the requests one by one gives what addSampleItems gives", "[core][samples][licenses]")
{
    RequestFixture f("sample-requests-run");
    const SampleItemRequests requests = sampleItemRequests(*f.lib.catalog, *f.materials, f.samples);
    plynth::test::requireOk(requests.status);
    for (const ImportRequest& request : requests.assets) {
        plynth::test::requireOk(importAsset(*f.lib.catalog, f.context, request).status);
    }
    for (const MaterialImportRequest& request : requests.materials) {
        plynth::test::requireOk(importMaterial(*f.materials, f.context, request).status);
    }
    const std::optional<AssetRecord> chair = f.lib.catalog->asset("chair");
    REQUIRE(chair.has_value());
    REQUIRE(chair->license == "cc0");
    REQUIRE(std::ranges::find(chair->categories, "chair") != chair->categories.end());
    const std::optional<MaterialRecord> oak = f.materials->material("oak_planks");
    REQUIRE(oak.has_value());
    REQUIRE(oak->license == "cc0");
    // The originals are kept.
    REQUIRE(fs::exists(f.samples / "models" / "chair" / "chair.obj"));
    REQUIRE(fs::exists(f.samples / "materials" / "oak_planks" / "material.json"));

    // A second time nothing is left to import.
    const SampleItemRequests again = sampleItemRequests(*f.lib.catalog, *f.materials, f.samples);
    REQUIRE(again.assets.empty());
    REQUIRE(again.materials.size() == 1);
    REQUIRE(again.materials[0].source.file == f.samples / "materials" / "plain_set");
}
