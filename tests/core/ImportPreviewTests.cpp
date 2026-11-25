#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Integrations.h>

#include "TestSupport.h"
#include "ZipTestWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

const char* const kId = "0c1d4a2e-1111-2222-3333-444455556666";

// A library folder (paths only) and a file or two to import.
struct Fixture {
    test::TempDir tmp{"preview"};
    LibraryPaths library = libraryPaths(tmp.path() / "Library");

    fs::path plainObj()
    {
        const fs::path file = tmp.path() / "box.obj";
        std::ofstream(file) << "v 0 0 0\n";
        return file;
    }
    fs::path variantsZip()
    {
        const fs::path file = tmp.path() / "variants.zip";
        test::writeZipFile(file, test::zipEntriesNamed({"Chair/FBX/Low/Chair_LOD0.fbx", "Chair/FBX/Medium/Chair_LOD0.fbx",
                                                         "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png",
                                                         "Chair/FBX/High/Chair_LOD0.fbx", "Chair/Chair.json"}));
        return file;
    }
    fs::path oakZip()
    {
        const fs::path file = tmp.path() / "oak.zip";
        test::writeZipFile(file, test::zipEntriesNamed({"Oak_2K_BaseColor.png", "Oak_2K_Normal.png", "Oak_4K_BaseColor.png",
                                                         "Oak_4K_Normal.png", "Oak.json"}));
        return file;
    }
    bool stagingEmpty() const
    {
        std::error_code ec;
        if (!fs::exists(library.assetStaging, ec)) return true;
        return fs::directory_iterator(library.assetStaging) == fs::directory_iterator();
    }
};

ImportRequest assetRequest(const fs::path& file)
{
    ImportRequest request;
    request.source.file = file;
    return request;
}

ImportRequest addressRequest(const std::string& text)
{
    ImportRequest request;
    request.source.url = text;
    return request;
}

}

TEST_CASE("an address is taken by the blenderkit importer", "[preview]")
{
    ImportSource address;
    address.url = std::string("https://www.blenderkit.com/asset-gallery-detail/") + kId + "/";
    REQUIRE(pickImporter(address));
    CHECK(pickImporter(address)->id() == kBlenderKitImporterId);
    REQUIRE(pickMaterialImporter(address));
    CHECK(pickMaterialImporter(address)->id() == kBlenderKitImporterId);
    CHECK(findImporter(kBlenderKitImporterId)->score(address) == 30);

    ImportSource file;
    file.file = "C:/x/chair.fbx";
    CHECK(findImporter(kBlenderKitImporterId)->score(file) == 0);
    CHECK(findMaterialImporter(kBlenderKitImporterId)->score(file) == 0);
    ImportSource other;
    other.url = "https://example.com/x";
    CHECK(pickImporter(other) == nullptr);
}

TEST_CASE("the blenderkit plan has a fetch and no file", "[preview]")
{
    Fixture f;
    const ImportPreview p = previewAssetImport(f.library, addressRequest(std::string("  asset_base_id:") + kId + " "));
    test::requireOk(p.status);
    CHECK(p.importer == "blenderkit");
    CHECK(p.integration == "blenderkit");
    CHECK(p.plan["file"].is_null());
    CHECK(p.plan["source_kind"] == "blenderkit");
    CHECK(p.plan["reference"] == std::string("asset_base_id:") + kId);
    CHECK(p.plan["fetch"]["task"] == "blenderkit_fetch");
    CHECK(p.plan["fetch"]["kind"] == "model");
    CHECK_FALSE(p.plan["fetch"].contains("window"));
    CHECK(p.plan["fetch"]["params"]["resolution"] == "2k");
    CHECK(p.plan["details"]["asset_base_id"] == kId);
    CHECK(f.stagingEmpty());

    MaterialImportRequest material;
    material.source.url = std::string("https://www.blenderkit.com/asset-gallery-detail/") + kId + "/";
    const ImportPreview m = previewMaterialImport(f.library, material);
    test::requireOk(m.status);
    CHECK(m.importer == "blenderkit");
    CHECK(m.plan["fetch"]["kind"] == "material");
    CHECK(m.plan["worker_source"].is_null());
}

TEST_CASE("saved blenderkit options reach the fetch", "[preview]")
{
    Fixture f;
    fs::create_directories(f.library.root);
    test::requireOk(setIntegrationOptions(f.library, "blenderkit", Json{{"resolution", "4k"}, {"blender_window", "minimized"}}));
    const ImportPreview p = previewAssetImport(f.library, addressRequest(std::string("asset_base_id:") + kId));
    test::requireOk(p.status);
    CHECK(p.plan["fetch"]["params"]["resolution"] == "4k");
    CHECK_FALSE(p.plan["fetch"].contains("window"));
    CHECK_FALSE(p.options.contains("blender_window"));
    CHECK(p.options["resolution"] == "4k");
}

TEST_CASE("a plain obj previews as local_file", "[preview]")
{
    Fixture f;
    const fs::path obj = f.plainObj();
    const ImportPreview p = previewAssetImport(f.library, assetRequest(obj));
    test::requireOk(p.status);
    CHECK(p.importer == "local_file");
    CHECK(p.integration == "local");
    CHECK(p.plan["file"] == pathToUtf8(obj));
    CHECK(p.plan["format"] == "obj");
    CHECK(p.plan["fetch"].is_null());
}

TEST_CASE("the variants zip previews as fab_zip and leaves nothing behind", "[preview]")
{
    Fixture f;
    const ImportPreview p = previewAssetImport(f.library, assetRequest(f.variantsZip()));
    test::requireOk(p.status);
    CHECK(p.importer == "fab_zip");
    CHECK(p.integration == "fab");
    CHECK(p.plan["format"] == "fab");
    CHECK(p.plan["original_is_zip"] == true);
    CHECK(p.plan["details"].is_object());
    CHECK_FALSE(p.plan["details"].empty());
    CHECK(f.stagingEmpty());
}

TEST_CASE("an importer can be named", "[preview]")
{
    Fixture f;
    const fs::path obj = f.plainObj();
    ImportRequest named = assetRequest(f.variantsZip());
    named.importer = "local_file";
    CHECK(previewAssetImport(f.library, named).importer == "local_file");

    ImportRequest plainZip;
    const fs::path zip = f.tmp.path() / "plain.zip";
    test::writeZipFile(zip, test::zipEntriesNamed({"box.obj"}));
    plainZip.source.file = zip;
    plainZip.importer = "fab_zip";
    const ImportPreview fab = previewAssetImport(f.library, plainZip);
    test::requireOk(fab.status);
    CHECK(fab.importer == "fab_zip");

    ImportRequest kit = assetRequest(obj);
    kit.importer = "blenderkit";
    const ImportPreview refused = previewAssetImport(f.library, kit);
    CHECK(refused.status.code == ErrorCode::InvalidValue);
    CHECK(refused.status.message == "BlenderKit cannot read this.");

    ImportRequest unknown = assetRequest(obj);
    unknown.importer = "nope";
    CHECK(previewAssetImport(f.library, unknown).status.code == ErrorCode::NotFound);
}

TEST_CASE("bad options and sources are refused", "[preview]")
{
    Fixture f;
    ImportRequest bad = assetRequest(f.variantsZip());
    bad.importerOptions = Json{{"quality", "ultra"}};
    CHECK(previewAssetImport(f.library, bad).status.code == ErrorCode::InvalidValue);

    ImportRequest kit = addressRequest(std::string("asset_base_id:") + kId);
    kit.importerOptions = Json{{"resolution", "4k"}};
    CHECK(previewAssetImport(f.library, kit).status.code == ErrorCode::InvalidValue);

    CHECK(previewAssetImport(f.library, assetRequest(f.tmp.path() / "missing.obj")).status.code == ErrorCode::NotFound);
    CHECK(previewAssetImport(f.library, ImportRequest{}).status.code == ErrorCode::InvalidValue);
    CHECK(previewAssetImport(f.library, addressRequest("https://example.com/x")).status.code == ErrorCode::InvalidValue);
}

TEST_CASE("a material zip previews and the json has its shape", "[preview]")
{
    Fixture f;
    MaterialImportRequest request;
    request.source.file = f.oakZip();
    const ImportPreview p = previewMaterialImport(f.library, request);
    test::requireOk(p.status);
    CHECK(p.importer == "fab_zip");

    const Json json = importPreviewToJson(p);
    CHECK(json["importer"] == "fab_zip");
    CHECK(json["integration"] == "fab");
    CHECK(json["options"].is_object());
    CHECK(json["plan"]["source_kind"] == "fab_zip");
    CHECK(json["plan"].contains("worker_source"));
    CHECK(json["plan"].contains("blend_material"));
    CHECK(json["plan"].contains("fetch"));
}
