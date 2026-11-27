#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/MaterialRescan.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A material library with a fake Blender and an import context for refreshes.
struct RescanFixture {
    explicit RescanFixture(const std::string& tag, const std::string& mode = "") : lib(tag)
    {
        const fs::path exe = plynth::test::makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", mode);
        context.library = lib.paths;
        context.blender = inspectBlender(exe);
        context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "scripts");
        context.removeOriginal = [](const fs::path&) { return Status{}; };
    }

    plynth::test::MaterialTestLibrary lib;
    ImportContext context;
};

}

TEST_CASE("opening the materials catalog indexes a library that was filled by hand", "[core][materialrescan]")
{
    plynth::test::TempDir tmp("mrescan-open");
    const fs::path root = tmp.path() / "Library";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths paths = libraryPaths(root);
    plynth::test::makeMaterialOnDisk(paths, "brick", "Brick");
    plynth::test::makeMaterialOnDisk(paths, "oak", "Oak");

    Status status;
    const std::unique_ptr<MaterialCatalog> catalog = openMaterialCatalog(paths, status);
    plynth::test::requireOk(status);
    REQUIRE(catalog != nullptr);
    REQUIRE(catalog->materialCount() == 2);
    REQUIRE(catalog->material("brick").has_value());
    REQUIRE(catalog->material("oak").has_value());
}

TEST_CASE("opening the materials catalog does not rescan an index that has materials", "[core][materialrescan]")
{
    plynth::test::MaterialTestLibrary lib("mrescan-indexed");
    lib.add("brick", "Brick");
    // A folder added by hand later waits for an explicit rescan.
    plynth::test::makeMaterialOnDisk(lib.paths, "oak", "Oak");
    lib.catalog.reset();

    Status status;
    const std::unique_ptr<MaterialCatalog> catalog = openMaterialCatalog(lib.paths, status);
    plynth::test::requireOk(status);
    REQUIRE(catalog->materialCount() == 1);
    REQUIRE_FALSE(catalog->material("oak").has_value());
}

TEST_CASE("an empty index with only hidden folders stays empty", "[core][materialrescan]")
{
    plynth::test::MaterialTestLibrary lib("mrescan-hidden");
    fs::create_directories(lib.paths.materialItems / ".partial");
    plynth::test::writeBytes(lib.paths.materialItems / "readme.txt", "not a folder");
    lib.catalog.reset();

    Status status;
    const std::unique_ptr<MaterialCatalog> catalog = openMaterialCatalog(lib.paths, status);
    plynth::test::requireOk(status);
    REQUIRE(catalog->materialCount() == 0);
}

TEST_CASE("opening the materials catalog of a folder that is no library fails like MaterialCatalog::open", "[core][materialrescan]")
{
    plynth::test::TempDir tmp("mrescan-nolib");
    Status status;
    const std::unique_ptr<MaterialCatalog> catalog = openMaterialCatalog(libraryPaths(tmp.path() / "Nothing"), status);
    REQUIRE(catalog == nullptr);
    Status direct;
    (void)MaterialCatalog::open(libraryPaths(tmp.path() / "Nothing"), direct);
    REQUIRE(status.code == direct.code);
    REQUIRE_FALSE(status.ok());
}

TEST_CASE("rescan renders every changed material again and reports each", "[core][materialrescan]")
{
    RescanFixture f("mrescan-refresh");
    f.lib.add("brick", "Brick");
    f.lib.add("oak", "Oak");
    f.lib.add("steady", "Steady");
    plynth::test::writeBytes(f.lib.folder("brick") / "textures" / "base_color.png", "changed on disk");
    plynth::test::writeBytes(f.lib.paths.materialItems / "raw_wood" / "wood_col.png", "image");

    std::vector<std::pair<int, int>> steps;
    const MaterialRescanResult result =
        rescanMaterials(*f.lib.catalog, f.context, true, [&](int index, int count) { steps.emplace_back(index, count); });
    REQUIRE(result.report.needsRefresh.size() == 2);
    REQUIRE(result.refreshed.size() == result.report.needsRefresh.size());
    for (std::size_t i = 0; i < result.refreshed.size(); ++i) {
        REQUIRE(result.refreshed[i].id == result.report.needsRefresh[i]);
        INFO(result.refreshed[i].status.message);
        REQUIRE(result.refreshed[i].status.ok());
    }
    REQUIRE(steps == std::vector<std::pair<int, int>>{{0, 2}, {1, 2}});
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::Ready);
    REQUIRE(f.lib.catalog->material("raw_wood")->state == MaterialState::Ready);
    REQUIRE(fs::exists(f.lib.folder("raw_wood") / "thumb.png"));
    REQUIRE(f.lib.catalog->rescan().needsRefresh.empty());
}

TEST_CASE("rescan without refresh only rebuilds the index", "[core][materialrescan]")
{
    RescanFixture f("mrescan-norefresh");
    f.lib.add("brick", "Brick");
    plynth::test::writeBytes(f.lib.folder("brick") / "textures" / "base_color.png", "changed on disk");

    int calls = 0;
    const MaterialRescanResult result = rescanMaterials(*f.lib.catalog, f.context, false, [&](int, int) { ++calls; });
    REQUIRE(result.report.needsRefresh == std::vector<std::string>{"brick"});
    REQUIRE(result.refreshed.empty());
    REQUIRE(calls == 0);
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::NeedsRefresh);
    REQUIRE(plynth::test::readTextFile(f.lib.folder("brick") / "textures" / "base_color.png") == "changed on disk");
}

TEST_CASE("rescan with nothing changed runs no Blender", "[core][materialrescan]")
{
    RescanFixture f("mrescan-nothing");
    f.lib.add("brick", "Brick");
    const MaterialRescanResult result = rescanMaterials(*f.lib.catalog, f.context, true);
    REQUIRE(result.report.needsRefresh.empty());
    REQUIRE(result.refreshed.empty());
    REQUIRE_FALSE(fs::exists(f.lib.tmp.path() / "blender" / "fake_jobs.log"));
}

TEST_CASE("a failed refresh is listed and the next material still goes on", "[core][materialrescan]")
{
    RescanFixture f("mrescan-fail", "fail");
    f.lib.add("brick", "Brick");
    f.lib.add("oak", "Oak");
    plynth::test::writeBytes(f.lib.folder("brick") / "textures" / "base_color.png", "changed on disk");
    plynth::test::writeBytes(f.lib.folder("oak") / "textures" / "base_color.png", "changed on disk");

    const MaterialRescanResult result = rescanMaterials(*f.lib.catalog, f.context, true);
    REQUIRE(result.refreshed.size() == 2);
    for (const MaterialRefreshResult& entry : result.refreshed) REQUIRE(entry.status.code == ErrorCode::BlenderFailed);
}

TEST_CASE("a rescan asked to stop refreshes nothing and lists each material as cancelled", "[core][materialrescan]")
{
    RescanFixture f("mrescan-stop");
    f.lib.add("brick", "Brick");
    plynth::test::writeBytes(f.lib.folder("brick") / "textures" / "base_color.png", "changed on disk");
    std::stop_source source;
    source.request_stop();

    const MaterialRescanResult result = rescanMaterials(*f.lib.catalog, f.context, true, {}, source.get_token());
    REQUIRE(result.refreshed.size() == 1);
    REQUIRE(result.refreshed[0].id == "brick");
    REQUIRE(result.refreshed[0].status.code == ErrorCode::Cancelled);
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::NeedsRefresh);
}
