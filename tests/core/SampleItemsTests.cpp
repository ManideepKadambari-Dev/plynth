#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetInfo.h>
#include <plynth/core/catalog/MaterialCatalog.h>
#include <plynth/core/catalog/SampleItems.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A small samples folder with one model and one material copied from the real samples, a library and a
// fake Blender.
struct SampleFixture {
    explicit SampleFixture(const std::string& tag) : lib(tag)
    {
        samples = lib.tmp.path() / "samples";
        const fs::path real = PLYNTH_SAMPLES_DIR;
        fs::create_directories(samples / "models");
        fs::create_directories(samples / "materials");
        fs::copy(real / "models" / "chair", samples / "models" / "chair", fs::copy_options::recursive);
        fs::copy(real / "materials" / "oak_planks", samples / "materials" / "oak_planks", fs::copy_options::recursive);
        std::ofstream(samples / "samples.json", std::ios::binary)
            << R"({"format": "plynth-samples", "version": 1, "license": "cc0",
 "assets": [{"id": "chair", "name": "Chair", "file": "models/chair/chair.obj", "categories": ["chair"], "tags": []}],
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

// The result for one kind and id, or null.
const SampleItemResult* find(const SampleItemsResult& result, const std::string& kind, const std::string& id)
{
    for (const SampleItemResult& item : result.items) {
        if (item.kind == kind && item.id == id) return &item;
    }
    return nullptr;
}

}

TEST_CASE("adding the sample items imports models and materials with cc0 and skips them the second time", "[core][samples][licenses]")
{
    SampleFixture f("sample-items");
    const std::string chairBefore = plynth::test::readTextFile(f.samples / "models" / "chair" / "chair.obj");

    const SampleItemsResult first = addSampleItems(*f.lib.catalog, *f.materials, f.context, f.samples);
    plynth::test::requireOk(first.status);
    const SampleItemResult* chair = find(first, "asset", "chair");
    REQUIRE(chair != nullptr);
    INFO(chair->status.message);
    CHECK(chair->status.ok());
    CHECK_FALSE(chair->skipped);
    const std::optional<AssetRecord> record = f.lib.catalog->asset("chair");
    REQUIRE(record.has_value());
    CHECK(record->license == "cc0");
    const SampleItemResult* oak = find(first, "material", "oak_planks");
    REQUIRE(oak != nullptr);
    CHECK(oak->status.ok());

    const SampleItemsResult second = addSampleItems(*f.lib.catalog, *f.materials, f.context, f.samples);
    plynth::test::requireOk(second.status);
    CHECK(find(second, "asset", "chair")->skipped);
    CHECK(find(second, "material", "oak_planks")->skipped);
    CHECK(plynth::test::readTextFile(f.samples / "models" / "chair" / "chair.obj") == chairBefore);
}

TEST_CASE("adding the sample items without an index is not found", "[core][samples]")
{
    SampleFixture f("sample-items-none");
    fs::remove(f.samples / "samples.json");
    CHECK(addSampleItems(*f.lib.catalog, *f.materials, f.context, f.samples).status.code == ErrorCode::NotFound);
}
