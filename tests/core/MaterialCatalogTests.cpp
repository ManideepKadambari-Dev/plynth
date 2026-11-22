#include "BuildFixtures.h"
#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/catalog/MaterialCatalog.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// The ids of the groups of a catalog in order.
std::vector<std::string> groupIds(const MaterialCatalog& catalog)
{
    std::vector<std::string> out;
    for (const MaterialGroupEntry& g : catalog.groups()) out.push_back(g.id);
    return out;
}

// Sets the write time of a file a minute ahead, so a map counts as changed.
void touchLater(const fs::path& file)
{
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::minutes(1));
}

// Reads info.json of a material straight from its folder.
MaterialInfo infoOnDisk(const MaterialTestLibrary& lib, const std::string& id)
{
    const MaterialInfoRead read = readMaterialInfo(lib.folder(id) / "info.json", id);
    requireOk(read.status);
    return read.info;
}

}

TEST_CASE("a new library has the built-in groups and licenses once", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-seed");
    const std::vector<std::string> expected = {"floor", "wall", "ceiling", "exterior", "trim", "door", "glass", "metal"};
    REQUIRE(groupIds(*lib.catalog) == expected);
    REQUIRE(builtInMaterialGroups().size() == expected.size());
    for (const MaterialGroupEntry& g : lib.catalog->groups()) {
        REQUIRE(g.builtIn);
        REQUIRE(g.materialCount == 0);
    }
    REQUIRE(lib.catalog->groups()[0].name == "Floor");
    REQUIRE(lib.catalog->licenses().size() == builtInLicenses().size());

    lib.reopen();
    lib.reopen();
    REQUIRE(groupIds(*lib.catalog) == expected);
    REQUIRE(lib.catalog->licenses().size() == builtInLicenses().size());
}

TEST_CASE("an empty groups table is seeded when the catalog opens", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-seed-old");
    for (const std::string& id : groupIds(*lib.catalog)) requireOk(lib.catalog->removeGroup(id));
    REQUIRE(lib.catalog->groups().empty());
    lib.reopen();
    REQUIRE(lib.catalog->groups().size() == builtInMaterialGroups().size());
}

TEST_CASE("the materials catalog needs a library", "[core][materialcatalog]") {
    TempDir tmp("matcat-nolib");
    Status status;
    REQUIRE_FALSE(MaterialCatalog::open(libraryPaths(tmp.path() / "Nothing"), status));
    REQUIRE(status.code == ErrorCode::NoLibrary);
}

TEST_CASE("groups can be added, renamed, moved and removed", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-groups");
    MaterialCatalog& c = *lib.catalog;

    requireOk(c.addGroup("carpet", "Carpet"));
    REQUIRE(groupIds(c).back() == "carpet");
    REQUIRE_FALSE(c.groups().back().builtIn);
    REQUIRE(c.addGroup("carpet", "Again").code == ErrorCode::AlreadyExists);
    REQUIRE(c.addGroup("Bad Id", "Bad").code == ErrorCode::InvalidValue);
    REQUIRE(c.addGroup("blank", "  ").code == ErrorCode::InvalidValue);

    lib.add("oak", "Oak planks", {"floor", "carpet"});
    lib.add("grey", "Grey tile", {"floor"});
    REQUIRE(c.groups()[0].materialCount == 2);

    requireOk(c.renameGroup("carpet", "Soft floor"));
    REQUIRE(c.groups().back().name == "Soft floor");
    REQUIRE(ids(c.search({.text = "soft"})) == std::vector<std::string>{"oak"});
    REQUIRE(c.renameGroup("nope", "X").code == ErrorCode::NotFound);

    requireOk(c.moveGroup("carpet", 0));
    REQUIRE(groupIds(c).front() == "carpet");
    requireOk(c.moveGroup("carpet", 99));
    REQUIRE(groupIds(c).back() == "carpet");
    REQUIRE(c.moveGroup("nope", 0).code == ErrorCode::NotFound);

    requireOk(c.removeGroup("carpet"));
    REQUIRE(c.removeGroup("carpet").code == ErrorCode::NotFound);
    REQUIRE(infoOnDisk(lib, "oak").groups == std::vector<std::string>{"floor"});
    REQUIRE(c.material("oak")->groups == std::vector<std::string>{"floor"});
    REQUIRE(c.search({.text = "soft"}).empty());
    REQUIRE(c.groups().size() == builtInMaterialGroups().size());
}

TEST_CASE("indexing a material adds the groups and the license it names", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-index-new");
    MaterialInfo info = makeMaterialOnDisk(lib.paths, "stone", "Stone", {"rock_face"});
    info.license = "mit";
    requireOk(writeMaterialInfo(lib.folder("stone") / "info.json", info));
    requireOk(lib.catalog->indexMaterial(info));
    REQUIRE(groupIds(*lib.catalog).back() == "rock_face");
    REQUIRE(lib.catalog->groups().back().name == "Rock face");

    bool hasLicense = false;
    for (const LicenseEntry& l : lib.catalog->licenses()) hasLicense = hasLicense || l.id == "mit";
    REQUIRE(hasLicense);

    MaterialInfo none;
    none.id = "ghost";
    REQUIRE(lib.catalog->indexMaterial(none).code == ErrorCode::NotFound);
    none.id = "Bad Id";
    REQUIRE(lib.catalog->indexMaterial(none).code == ErrorCode::InvalidValue);
}

TEST_CASE("a material reads back from the index and info", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-read");
    const MaterialInfo made = lib.add("oak", "Oak planks", {"floor"}, {"wood", "warm"}, true, true);
    const auto record = lib.catalog->material("oak");
    REQUIRE(record);
    REQUIRE(record->name == "Oak planks");
    REQUIRE(record->tags == std::vector<std::string>{"warm", "wood"});
    REQUIRE(record->state == MaterialState::Ready);
    REQUIRE(record->thumbnail == ThumbnailState::Ok);
    REQUIRE(record->thumbFile == lib.folder("oak") / "thumb.png");
    REQUIRE(record->tileSize == TileSize{1.0, 0.5});
    REQUIRE(record->sourceKind == "texture_set");
    REQUIRE_FALSE(record->uncategorized());
    REQUIRE_FALSE(lib.catalog->material("nope"));

    const MaterialInfoRead info = lib.catalog->info("oak");
    requireOk(info.status);
    REQUIRE(info.info == made);
    REQUIRE(lib.catalog->info("nope").status.code == ErrorCode::NotFound);

    lib.add("plain", "Plain", {}, {}, false);
    REQUIRE(lib.catalog->material("plain")->thumbnail == ThumbnailState::Missing);
    REQUIRE(lib.catalog->material("plain")->thumbFile.empty());
    REQUIRE(lib.catalog->material("plain")->uncategorized());
    REQUIRE(lib.catalog->materialCount() == 2);
}

TEST_CASE("a failed thumbnail shows as failed", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-thumb-failed");
    MaterialInfo info = makeMaterialOnDisk(lib.paths, "oak", "Oak", {}, {}, false);
    info.thumbnail.error = "Eevee did not start.";
    requireOk(writeMaterialInfo(lib.folder("oak") / "info.json", info));
    requireOk(lib.catalog->indexMaterial(info));
    const auto record = lib.catalog->material("oak");
    REQUIRE(record->thumbnail == ThumbnailState::Failed);
    REQUIRE(record->thumbnailError == "Eevee did not start.");
}

TEST_CASE("search by words, group, tags, license, uncategorized and limit", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-search");
    lib.add("oak_planks", "Oak planks", {"floor"}, {"wood", "warm"});
    lib.add("red_brick", "Red brick", {"wall", "exterior"}, {"brick", "red"});
    lib.add("grey_tile", "Grey tile", {"floor", "wall"}, {"tile"});
    lib.add("mystery", "Mystery", {}, {});
    requireOk(lib.catalog->editMaterial("red_brick", {.license = "own_work"}));
    MaterialCatalog& c = *lib.catalog;

    REQUIRE(ids(c.search({})) == std::vector<std::string>{"grey_tile", "mystery", "oak_planks", "red_brick"});
    REQUIRE(ids(c.search({.text = "bri"})) == std::vector<std::string>{"red_brick"});
    REQUIRE(ids(c.search({.text = "oak pl"})) == std::vector<std::string>{"oak_planks"});
    REQUIRE(ids(c.search({.text = "oak brick"})).empty());
    REQUIRE(ids(c.search({.text = "exterior"})) == std::vector<std::string>{"red_brick"});
    REQUIRE(ids(c.search({.text = "WARM"})) == std::vector<std::string>{"oak_planks"});
    REQUIRE(ids(c.search({.text = "red_brick"})) == std::vector<std::string>{"red_brick"});
    REQUIRE(ids(c.search({.group = "floor"})) == std::vector<std::string>{"grey_tile", "oak_planks"});
    REQUIRE(ids(c.search({.text = "tile", .group = "floor"})) == std::vector<std::string>{"grey_tile"});
    REQUIRE(ids(c.search({.tags = {"red", "brick"}})) == std::vector<std::string>{"red_brick"});
    REQUIRE(ids(c.search({.tags = {"red", "wood"}})).empty());
    REQUIRE(ids(c.search({.license = "own_work"})) == std::vector<std::string>{"red_brick"});
    REQUIRE(ids(c.search({.license = "unknown"})).size() == 3);
    REQUIRE(ids(c.search({.uncategorizedOnly = true})) == std::vector<std::string>{"mystery"});
    REQUIRE(ids(c.search({.limit = 2})) == std::vector<std::string>{"grey_tile", "mystery"});
}

TEST_CASE("editing a material rewrites info.json and the index", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-edit");
    lib.add("oak", "Oak planks", {"floor"}, {"wood"});
    MaterialCatalog& c = *lib.catalog;

    requireOk(c.editMaterial("oak", {.name = "Oak boards", .groups = std::vector<std::string>{"floor", "trim", "floor"},
                                     .tags = std::vector<std::string>{"b", "a", "b"}, .license = "cc0"}));
    const MaterialInfo info = infoOnDisk(lib, "oak");
    REQUIRE(info.name == "Oak boards");
    REQUIRE(info.groups == std::vector<std::string>{"floor", "trim"});
    REQUIRE(info.tags == std::vector<std::string>{"a", "b"});
    REQUIRE(info.license == "cc0");
    const auto record = c.material("oak");
    REQUIRE(record->name == "Oak boards");
    REQUIRE(record->state == MaterialState::Ready);
    REQUIRE(ids(c.search({.text = "boards"})) == std::vector<std::string>{"oak"});

    REQUIRE(c.editMaterial("nope", {.name = "X"}).code == ErrorCode::NotFound);
    REQUIRE(c.editMaterial("oak", {.groups = std::vector<std::string>{"nope"}}).code == ErrorCode::NotFound);
    REQUIRE(c.editMaterial("oak", {.license = "nope"}).code == ErrorCode::NotFound);
    REQUIRE(c.editMaterial("oak", {.name = " "}).code == ErrorCode::InvalidValue);
    REQUIRE(c.editMaterial("oak", {.tags = std::vector<std::string>{"Bad Tag"}}).code == ErrorCode::InvalidValue);
    REQUIRE(infoOnDisk(lib, "oak") == info);
}

TEST_CASE("a new tile size makes the material need a refresh", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-tile");
    lib.add("oak", "Oak planks", {"floor"});
    MaterialCatalog& c = *lib.catalog;

    requireOk(c.editMaterial("oak", {.tileSize = TileSize{1.0, 0.5}}));
    REQUIRE(c.material("oak")->state == MaterialState::Ready);

    REQUIRE(c.editMaterial("oak", {.tileSize = TileSize{0.0, 1.0}}).code == ErrorCode::InvalidValue);
    REQUIRE(c.material("oak")->state == MaterialState::Ready);

    requireOk(c.editMaterial("oak", {.tileSize = TileSize{2.0, 1.0}}));
    auto record = c.material("oak");
    REQUIRE(record->state == MaterialState::NeedsRefresh);
    REQUIRE(record->tileSize == TileSize{2.0, 1.0});
    REQUIRE(infoOnDisk(lib, "oak").tileSize == TileSize{2.0, 1.0});

    // The state comes from the files, so it survives a rescan and a new run.
    const RescanReport report = c.rescan();
    REQUIRE(report.needsRefresh == std::vector<std::string>{"oak"});
    lib.reopen();
    REQUIRE(lib.catalog->material("oak")->state == MaterialState::NeedsRefresh);
    REQUIRE(materialForUse(*lib.catalog, "oak").status.ok());
}

TEST_CASE("materialForUse says ready, needs refresh, broken and unknown", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-foruse");
    lib.add("oak_planks", "Oak planks");
    lib.add("slow", "Slow");
    lib.add("gone", "Gone");
    requireOk(lib.catalog->editMaterial("slow", {.tileSize = TileSize{3.0, 3.0}}));
    fs::remove(lib.folder("gone") / "textures" / "normal.png");
    lib.catalog->rescan();

    const MaterialForUse ready = materialForUse(*lib.catalog, "oak_planks");
    REQUIRE(ready.status.ok());
    REQUIRE(ready.name == "Oak planks");

    REQUIRE(materialForUse(*lib.catalog, "slow").status.ok());

    const MaterialForUse broken = materialForUse(*lib.catalog, "gone");
    REQUIRE(broken.status.code == ErrorCode::InvalidValue);
    REQUIRE(broken.status.message.find("textures/normal.png") != std::string::npos);

    const MaterialForUse unknown = materialForUse(*lib.catalog, "oak_planksx");
    REQUIRE(unknown.status.code == ErrorCode::NotFound);
    REQUIRE(unknown.status.message == "There is no material oak_planksx in the library.");
}

TEST_CASE("rescan indexes a new folder, a changed map, a deleted map and a removed folder", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-rescan");
    lib.add("oak", "Oak");
    lib.add("brick", "Brick");
    lib.add("tile", "Tile");
    lib.add("paint", "Paint");
    MaterialCatalog& c = *lib.catalog;

    RescanReport report = c.rescan();
    REQUIRE(report.unchanged == 4);
    REQUIRE(report.added == 0);
    REQUIRE(report.needsRefresh.empty());
    REQUIRE(report.broken.empty());

    writeBytes(lib.folder("fresh") / "diffuse.png", "IMG");
    writeBytes(lib.folder("fresh") / "nor.png", "IMG");
    touchLater(lib.folder("oak") / "textures" / "roughness.jpg");
    fs::remove(lib.folder("brick") / "textures" / "metallic.png");
    fs::remove_all(lib.folder("paint"));
    writeBytes(lib.paths.materialInbox / "pack.zip", "ZIP");
    writeBytes(lib.paths.materialInbox / "done" / "old.zip", "ZIP");

    report = c.rescan();
    REQUIRE(report.added == 1);
    REQUIRE(report.updated == 2);
    REQUIRE(report.unchanged == 1);
    REQUIRE(report.removed == 1);
    REQUIRE(report.inboxWaiting == 1);
    REQUIRE(report.needsRefresh == std::vector<std::string>{"fresh", "oak"});
    REQUIRE(report.broken.size() == 1);
    REQUIRE(report.broken[0].first == "brick");

    const auto fresh = c.material("fresh");
    REQUIRE(fresh);
    REQUIRE(fresh->state == MaterialState::NeedsRefresh);
    REQUIRE(fresh->problem == "Not cleaned yet.");
    REQUIRE(fresh->name == "Fresh");
    REQUIRE(fresh->uncategorized());
    REQUIRE(c.info("fresh").status.code == ErrorCode::CannotRead);

    REQUIRE(c.material("oak")->state == MaterialState::NeedsRefresh);
    REQUIRE(c.material("brick")->state == MaterialState::Broken);
    REQUIRE(c.material("tile")->state == MaterialState::Ready);
    REQUIRE_FALSE(c.material("paint"));
    REQUIRE(c.materialCount() == 4);

    // A second scan changes nothing but still lists what needs work.
    report = c.rescan();
    REQUIRE(report.added == 0);
    REQUIRE(report.updated == 0);
    REQUIRE(report.removed == 0);
    REQUIRE(report.unchanged == 4);
    REQUIRE(report.needsRefresh == std::vector<std::string>{"fresh", "oak"});
}

TEST_CASE("rescan marks bad folders and skips hidden ones", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-rescan-bad");
    lib.add("good", "Good");
    writeBytes(lib.folder("junk") / "readme.txt", "hello");
    writeBytes(lib.folder("badinfo") / "info.json", "{not json");
    writeBytes(lib.folder("Not A Material") / "x.png", "IMG");
    writeBytes(lib.folder(".hidden") / "x.png", "IMG");
    makeMaterialOnDisk(lib.paths, "other", "Other");
    fs::rename(lib.folder("other"), lib.folder("renamed"));

    const RescanReport report = lib.catalog->rescan();
    REQUIRE(lib.catalog->material("good")->state == MaterialState::Ready);
    REQUIRE(lib.catalog->material("junk")->state == MaterialState::Broken);
    REQUIRE(lib.catalog->material("badinfo")->state == MaterialState::Broken);
    REQUIRE(lib.catalog->material("renamed")->state == MaterialState::Broken);
    REQUIRE_FALSE(lib.catalog->material(".hidden"));
    REQUIRE(report.broken.size() == 4);
    REQUIRE(lib.catalog->search({.text = "good"}).size() == 1);
}

TEST_CASE("rescan rebuilds an index that was lost", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-rescan-lost");
    lib.add("oak", "Oak planks", {"floor"}, {"wood"});
    lib.catalog.reset();
    fs::remove(lib.paths.materialsDb);
    fs::remove(fs::path(lib.paths.materialsDb) += "-wal");
    fs::remove(fs::path(lib.paths.materialsDb) += "-shm");
    lib.reopen();
    REQUIRE(lib.catalog->materialCount() == 0);
    const RescanReport report = lib.catalog->rescan();
    REQUIRE(report.added == 1);
    const auto record = lib.catalog->material("oak");
    REQUIRE(record);
    REQUIRE(record->groups == std::vector<std::string>{"floor"});
    REQUIRE(record->tags == std::vector<std::string>{"wood"});
    REQUIRE(ids(lib.catalog->search({.text = "planks"})) == std::vector<std::string>{"oak"});
}

TEST_CASE("idTaken and removeMaterial", "[core][materialcatalog]") {
    MaterialTestLibrary lib("matcat-remove");
    lib.add("oak", "Oak planks", {"floor"});
    MaterialCatalog& c = *lib.catalog;

    REQUIRE(c.idTaken("oak"));
    REQUIRE_FALSE(c.idTaken("brick"));
    writeBytes(lib.folder("stray") / "a.png", "IMG");
    REQUIRE(c.idTaken("stray"));
    markFolderLive(lib.paths.materialStaging / "brick-1");
    REQUIRE(c.idTaken("brick"));

    requireOk(c.removeMaterial("oak"));
    REQUIRE_FALSE(fs::exists(lib.folder("oak")));
    REQUIRE_FALSE(c.material("oak"));
    REQUIRE(c.search({.text = "oak"}).empty());
    REQUIRE(c.groups()[0].materialCount == 0);
    REQUIRE(c.removeMaterial("oak").code == ErrorCode::NotFound);
    REQUIRE(c.removeMaterial("../escape").code == ErrorCode::InvalidValue);
}

TEST_CASE("buildMaterialSourceFor gives map paths, broken and nothing", "[core][materialcatalog][build]") {
    MaterialTestLibrary lib("matcat-source");
    lib.add("white_paint", "White paint", {"wall"}, {}, true, true);
    lib.add("hurt", "Hurt");
    lib.add("noinfo", "No info");
    fs::remove(lib.folder("hurt") / "textures" / "roughness.jpg");
    fs::remove(lib.folder("noinfo") / "info.json");
    lib.catalog->rescan();
    const BuildMaterialSource source = buildMaterialSourceFor(*lib.catalog);

    const auto ready = source("white_paint");
    REQUIRE(ready);
    REQUIRE_FALSE(ready->broken);
    const fs::path textures = lib.folder("white_paint") / "textures";
    REQUIRE(ready->material.id == "white_paint");
    REQUIRE(ready->material.tileSize == TileSize{1.0, 0.5});
    REQUIRE(ready->material.baseColor == textures / "base_color.png");
    REQUIRE(ready->material.normal == textures / "normal.png");
    REQUIRE(ready->material.roughness == textures / "roughness.jpg");
    REQUIRE(ready->material.metallic == textures / "metallic.png");
    REQUIRE(ready->material.opacity == textures / "opacity.png");
    REQUIRE(ready->material.occlusion.empty());
    REQUIRE(ready->material.baseColor.is_absolute());

    const auto hurt = source("hurt");
    REQUIRE(hurt);
    REQUIRE(hurt->broken);
    REQUIRE(hurt->problem.find("roughness") != std::string::npos);

    const auto noInfo = source("noinfo");
    REQUIRE(noInfo);
    REQUIRE(noInfo->broken);
    REQUIRE_FALSE(noInfo->problem.empty());

    REQUIRE_FALSE(source("not_there"));
    REQUIRE_FALSE(source(""));
}

TEST_CASE("buildMaterialSourceFor reads info.json of a material once", "[core][materialcatalog][build]") {
    MaterialTestLibrary lib("matcat-source-once");
    lib.add("white_paint", "White paint");
    const BuildMaterialSource source = buildMaterialSourceFor(*lib.catalog);
    const auto first = source("white_paint");
    REQUIRE(first);
    fs::remove(lib.folder("white_paint") / "info.json");
    const auto second = source("white_paint");
    REQUIRE(second);
    REQUIRE(second->material == first->material);
    REQUIRE_FALSE(second->broken);
}

TEST_CASE("runBuild gives the library materials to the job", "[core][materialcatalog][build][run]") {
    TestLibrary assets("matcat-build");
    assets.add("sofa", "Sofa", {"sofa"});
    const LibraryPaths& paths = assets.paths;
    makeMaterialOnDisk(paths, "white_paint", "White paint", {"wall"});
    makeMaterialOnDisk(paths, "red_brick", "Red brick", {"wall"});
    {
        Status status;
        auto materials = MaterialCatalog::open(paths, status);
        requireOk(status);
        REQUIRE(materials->rescan().broken.empty());
        REQUIRE(materials->materialCount() == 2);
    }

    const fs::path projectFolder = assets.tmp.path() / "Project";
    fs::create_directories(projectFolder);
    BuildRequest request;
    request.projectFolder = projectFolder;
    request.project = buildProject();
    setMaterialDefault(request.project.settings.materials, Surface::WallInside, "white_paint");
    setMaterialDefault(request.project.settings.materials, Surface::WallOutside, "red_brick");
    setMaterialDefault(request.project.settings.materials, Surface::Floor, "oak_planks");
    request.library = paths;
    const fs::path exe = makeFakeBlender(assets.tmp.path() / "blender", "5.2.0", "");
    request.blender = inspectBlender(exe);
    request.run.scriptsFolder = makeFakeScripts(assets.tmp.path() / "scripts");

    const BuildReport report = runBuild(request);
    requireOk(report.status);

    const nlohmann::json job = nlohmann::json::parse(readTextFile(projectFolder / "build" / "job.json"));
    const nlohmann::json& listed = job["params"]["materials"];
    REQUIRE(listed.size() == 2);
    REQUIRE(listed[0]["id"] == "red_brick");
    REQUIRE(listed[1]["id"] == "white_paint");
    const fs::path expected = paths.materialItems / "white_paint" / "textures" / "base_color.png";
    REQUIRE(pathFromUtf8(listed[1]["maps"]["base_color"].get<std::string>()) == expected);

    bool missing = false;
    for (const BuildProblem& p : report.problems) {
        if (p.code == "missing_material") missing = true;
    }
    REQUIRE(missing);
    for (const BuildProblem& p : report.problems) REQUIRE(p.code != "no_library");
}
