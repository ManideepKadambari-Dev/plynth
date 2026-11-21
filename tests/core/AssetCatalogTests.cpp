#include "CatalogSupport.h"

#include <plynth/core/RoomTypes.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;
using plynth::test::TestLibrary;

namespace {

// Puts the built-in room types back when a test ends.
struct RestoreRoomTypes {
    ~RestoreRoomTypes() { useRoomTypes({}); }
};

// The ids of categories.
std::vector<std::string> categoryIds(const std::vector<CategoryEntry>& list)
{
    std::vector<std::string> out;
    for (const CategoryEntry& c : list) out.push_back(c.id);
    return out;
}

}

TEST_CASE("a new library has the built-in lists", "[core][catalog]") {
    TestLibrary lib("cat-builtin");
    const auto categories = lib.catalog->categories();
    REQUIRE(categories.size() == 24);
    REQUIRE(categories.front().id == "sofa");
    REQUIRE(categories[9].id == "tv");
    REQUIRE(categories[9].name == "TV");
    REQUIRE(categories[16].id == "kitchen_unit");
    REQUIRE(categories.back().id == "window");
    for (std::size_t i = 0; i < categories.size(); ++i) {
        REQUIRE(categories[i].id == builtInCategories()[i].id);
        REQUIRE(categories[i].builtIn);
        REQUIRE(categories[i].assetCount == 0);
    }

    const auto licenses = lib.catalog->licenses();
    REQUIRE(licenses.size() == 6);
    REQUIRE(licenses[0].id == "unknown");
    REQUIRE(licenses[1].id == "cc0");
    REQUIRE(licenses[1].redistributable);
    REQUIRE_FALSE(licenses[4].redistributable);

    const auto rooms = lib.catalog->roomTypes();
    REQUIRE(rooms.size() == 9);
    REQUIRE(rooms[0].id == "living");
    REQUIRE(rooms[0].categories ==
            std::vector<std::string>{"sofa", "armchair", "table", "shelf", "tv", "lamp", "carpet", "curtain", "plant", "decor"});
    REQUIRE(rooms[8].id == "other");
    REQUIRE(rooms[8].categories.empty());
    REQUIRE(categoryIds(lib.catalog->categoriesFor("storage")) == std::vector<std::string>{"cabinet", "shelf"});
    REQUIRE(lib.catalog->categoriesFor("nothing").empty());
}

TEST_CASE("roomTypeList equals the built-in room types", "[core][catalog]") {
    TestLibrary lib("cat-roomlist");
    const auto list = lib.catalog->roomTypeList();
    REQUIRE(list.size() == builtInRoomTypes().size());
    for (std::size_t i = 0; i < list.size(); ++i) REQUIRE(list[i] == builtInRoomTypes()[i]);
}

TEST_CASE("room type edits", "[core][catalog]") {
    TestLibrary lib("cat-rooms");
    AssetCatalog& c = *lib.catalog;

    plynth::test::requireOk(c.addRoomType("gaming", "Gaming room"));
    REQUIRE(c.roomTypes().back().id == "gaming");
    REQUIRE_FALSE(c.roomTypes().back().builtIn);
    REQUIRE(c.addRoomType("gaming", "Again").code == ErrorCode::AlreadyExists);
    REQUIRE(c.addRoomType("Bad Id", "X").code == ErrorCode::InvalidValue);
    REQUIRE(c.addRoomType("fine", "").code == ErrorCode::InvalidValue);

    plynth::test::requireOk(c.renameRoomType("gaming", "Game room"));
    REQUIRE(c.roomTypeList().back().name == "Game room");
    REQUIRE(c.renameRoomType("nope", "X").code == ErrorCode::NotFound);

    plynth::test::requireOk(c.moveRoomType("gaming", 0));
    REQUIRE(c.roomTypeList().front().id == "gaming");
    REQUIRE(c.roomTypeList()[1].id == "living");
    plynth::test::requireOk(c.moveRoomType("gaming", 99));
    REQUIRE(c.roomTypeList().back().id == "gaming");
    REQUIRE(c.moveRoomType("nope", 1).code == ErrorCode::NotFound);

    plynth::test::requireOk(c.setRoomTypeCategories("gaming", {"desk", "chair", "tv", "lamp", "desk"}));
    REQUIRE(c.roomTypes().back().categories == std::vector<std::string>{"chair", "desk", "tv", "lamp"});
    REQUIRE(c.setRoomTypeCategories("gaming", {"desk", "spaceship"}).code == ErrorCode::NotFound);
    REQUIRE(c.roomTypes().back().categories.size() == 4);
    REQUIRE(c.setRoomTypeCategories("nope", {}).code == ErrorCode::NotFound);

    plynth::test::requireOk(c.removeRoomType("gaming"));
    REQUIRE(c.roomTypeList().size() == 9);
    REQUIRE(c.removeRoomType("gaming").code == ErrorCode::NotFound);
    plynth::test::requireOk(c.removeRoomType("living"));
    REQUIRE(c.roomTypeList().front().id == "bedroom");
    REQUIRE(c.categoriesFor("living").empty());
}

TEST_CASE("category edits", "[core][catalog]") {
    TestLibrary lib("cat-categories");
    AssetCatalog& c = *lib.catalog;
    plynth::test::requireOk(c.addCategory("rug", "Rug"));
    REQUIRE(c.categories().back().id == "rug");
    REQUIRE(c.addCategory("rug", "Rug").code == ErrorCode::AlreadyExists);
    REQUIRE(c.addCategory("Rug 2", "Rug").code == ErrorCode::InvalidValue);
    plynth::test::requireOk(c.renameCategory("rug", "Floor rug"));
    REQUIRE(c.categories().back().name == "Floor rug");
    REQUIRE(c.renameCategory("nope", "X").code == ErrorCode::NotFound);
    plynth::test::requireOk(c.removeCategory("rug"));
    REQUIRE(c.categories().size() == 24);
    REQUIRE(c.removeCategory("rug").code == ErrorCode::NotFound);
}

TEST_CASE("removeCategory takes it off assets and room types", "[core][catalog]") {
    TestLibrary lib("cat-removecat");
    lib.add("couch", "Couch", {"sofa", "decor"}, {"soft"});
    lib.add("loveseat", "Loveseat", {"sofa"});
    lib.add("vase", "Vase", {"decor"});
    AssetCatalog& c = *lib.catalog;
    REQUIRE(c.categories()[0].assetCount == 2);

    plynth::test::requireOk(c.removeCategory("sofa"));
    REQUIRE(c.asset("couch")->categories == std::vector<std::string>{"decor"});
    REQUIRE(c.asset("loveseat")->categories.empty());
    REQUIRE(c.asset("loveseat")->uncategorized());
    REQUIRE(c.asset("vase")->categories == std::vector<std::string>{"decor"});
    const AssetInfoRead info = readAssetInfo(lib.paths.assetItems / "couch" / "info.json", "couch");
    plynth::test::requireOk(info.status);
    REQUIRE(info.info.categories == std::vector<std::string>{"decor"});
    REQUIRE(readAssetInfo(lib.paths.assetItems / "loveseat" / "info.json").info.categories.empty());
    const auto living = c.roomTypes()[0].categories;
    REQUIRE(std::find(living.begin(), living.end(), "sofa") == living.end());
    AssetQuery query;
    query.text = "sofa";
    REQUIRE(c.search(query).empty());
}

TEST_CASE("useLibraryRoomTypes makes a library's room types the ones in use", "[core][catalog]") {
    RestoreRoomTypes restore;
    TestLibrary lib("cat-use-rooms");
    plynth::test::requireOk(lib.catalog->addRoomType("gaming", "Gaming room"));
    REQUIRE(checkRoomType("gaming").code == ErrorCode::InvalidValue);
    plynth::test::requireOk(AssetCatalog::useLibraryRoomTypes(lib.paths));
    REQUIRE(checkRoomType("gaming").ok());
    REQUIRE(checkRoomType("living").ok());
    REQUIRE(roomTypeName(std::string("gaming")) == "Gaming room");
}

TEST_CASE("useLibraryRoomTypes on a folder that is not a library keeps the list", "[core][catalog]") {
    RestoreRoomTypes restore;
    plynth::test::TempDir tmp("cat-use-none");
    const Status s = AssetCatalog::useLibraryRoomTypes(libraryPaths(tmp.path() / "nothing"));
    REQUIRE(s.code == ErrorCode::NoLibrary);
    REQUIRE(checkRoomType("living").ok());
}

TEST_CASE("opening a catalog on a folder that is not a library", "[core][catalog]") {
    plynth::test::TempDir tmp("cat-open-none");
    Status status;
    REQUIRE(AssetCatalog::open(libraryPaths(tmp.path()), status) == nullptr);
    REQUIRE(status.code == ErrorCode::NoLibrary);
}

TEST_CASE("indexAsset adds categories the catalog does not know", "[core][catalog]") {
    TestLibrary lib("cat-index-unknown");
    lib.add("rugged", "Rugged", {"rug"});
    bool found = false;
    for (const CategoryEntry& c : lib.catalog->categories()) {
        if (c.id == "rug") {
            found = true;
            REQUIRE(c.name == "Rug");
            REQUIRE_FALSE(c.builtIn);
            REQUIRE(c.assetCount == 1);
        }
    }
    REQUIRE(found);
}

TEST_CASE("an indexed asset reads back as a record", "[core][catalog]") {
    TestLibrary lib("cat-record");
    lib.add("chair", "Chair", {"chair"}, {"wood", "oak"}, true);
    lib.add("plain", "Plain");
    const auto r = lib.catalog->asset("chair");
    REQUIRE(r);
    REQUIRE(r->name == "Chair");
    REQUIRE(r->tags == std::vector<std::string>{"oak", "wood"});
    REQUIRE(r->license == "unknown");
    REQUIRE(r->sourceKind == "local_file");
    REQUIRE(r->sourceReference == "chair.obj");
    REQUIRE(r->size.height == 0.9);
    REQUIRE(r->triangles == 12);
    REQUIRE(r->state == AssetState::Ready);
    REQUIRE(r->thumbnail == ThumbnailState::Ok);
    REQUIRE(r->folder == lib.paths.assetItems / "chair");
    REQUIRE(r->thumbFile == lib.paths.assetItems / "chair" / "thumb.png");
    REQUIRE(lib.catalog->asset("plain")->thumbnail == ThumbnailState::Missing);
    REQUIRE(lib.catalog->asset("plain")->thumbFile.empty());
    REQUIRE_FALSE(lib.catalog->asset("nothing"));
    REQUIRE(lib.catalog->assetCount() == 2);
}

TEST_CASE("a failed thumbnail is Failed with its error", "[core][catalog]") {
    TestLibrary lib("cat-thumb-failed");
    AssetInfo info = plynth::test::makeAssetOnDisk(lib.paths, "lamp", "Lamp");
    info.thumbnail.error = "Fake render failure";
    plynth::test::requireOk(writeAssetInfo(lib.paths.assetItems / "lamp" / "info.json", info));
    plynth::test::requireOk(lib.catalog->indexAsset(info));
    const auto r = lib.catalog->asset("lamp");
    REQUIRE(r->thumbnail == ThumbnailState::Failed);
    REQUIRE(r->thumbnailError == "Fake render failure");
}

TEST_CASE("editAsset", "[core][catalog]") {
    TestLibrary lib("cat-edit");
    lib.add("chair", "Chair", {"chair"}, {"oak"});
    AssetCatalog& c = *lib.catalog;
    const fs::path file = lib.paths.assetItems / "chair" / "info.json";

    AssetEdit edit;
    edit.name = "Dining chair";
    edit.categories = std::vector<std::string>{"chair", "table", "chair"};
    edit.tags = std::vector<std::string>{"wood", "oak", "wood"};
    edit.license = "cc0";
    plynth::test::requireOk(c.editAsset("chair", edit));
    const auto r = c.asset("chair");
    REQUIRE(r->name == "Dining chair");
    REQUIRE(r->categories == std::vector<std::string>{"chair", "table"});
    REQUIRE(r->tags == std::vector<std::string>{"oak", "wood"});
    REQUIRE(r->license == "cc0");
    const AssetInfo info = readAssetInfo(file, "chair").info;
    REQUIRE(info.name == "Dining chair");
    REQUIRE(info.categories == std::vector<std::string>{"chair", "table"});
    REQUIRE(info.tags == std::vector<std::string>{"oak", "wood"});
    REQUIRE(info.license == "cc0");
    AssetQuery query;
    query.text = "dining";
    REQUIRE(c.search(query).size() == 1);
    REQUIRE(c.asset("chair")->state == AssetState::Ready);

    AssetEdit clear;
    clear.categories = std::vector<std::string>{};
    plynth::test::requireOk(c.editAsset("chair", clear));
    REQUIRE(c.asset("chair")->uncategorized());
}

TEST_CASE("editAsset refusals leave info.json unchanged", "[core][catalog]") {
    TestLibrary lib("cat-edit-bad");
    lib.add("chair", "Chair", {"chair"});
    AssetCatalog& c = *lib.catalog;
    const fs::path file = lib.paths.assetItems / "chair" / "info.json";
    const std::string before = plynth::test::readTextFile(file);

    AssetEdit unknownCategory;
    unknownCategory.categories = std::vector<std::string>{"spaceship"};
    REQUIRE(c.editAsset("chair", unknownCategory).code == ErrorCode::NotFound);
    AssetEdit unknownLicense;
    unknownLicense.license = "free_beer";
    REQUIRE(c.editAsset("chair", unknownLicense).code == ErrorCode::NotFound);
    AssetEdit badTag;
    badTag.tags = std::vector<std::string>{"Bad Tag"};
    REQUIRE(c.editAsset("chair", badTag).code == ErrorCode::InvalidValue);
    AssetEdit emptyName;
    emptyName.name = "";
    REQUIRE(c.editAsset("chair", emptyName).code == ErrorCode::InvalidValue);
    AssetEdit rename;
    rename.name = "X";
    REQUIRE(c.editAsset("nothing", rename).code == ErrorCode::NotFound);
    REQUIRE(plynth::test::readTextFile(file) == before);
    REQUIRE(c.asset("chair")->name == "Chair");
}

TEST_CASE("removeAsset", "[core][catalog]") {
    TestLibrary lib("cat-remove-asset");
    lib.add("chair", "Chair", {"chair"});
    AssetCatalog& c = *lib.catalog;
    plynth::test::requireOk(c.removeAsset("chair"));
    REQUIRE_FALSE(fs::exists(lib.paths.assetItems / "chair"));
    REQUIRE_FALSE(c.asset("chair"));
    AssetQuery query;
    query.text = "chair";
    REQUIRE(c.search(query).empty());
    REQUIRE(c.removeAsset("chair").code == ErrorCode::NotFound);
}

TEST_CASE("idTaken sees rows, folders and running imports", "[core][catalog]") {
    TestLibrary lib("cat-idtaken");
    lib.add("chair", "Chair");
    plynth::test::writeBytes(lib.paths.assetItems / "folder_only" / "asset.blend", "x");
    plynth::test::writeBytes(lib.paths.assetStaging / "busy-3f9a07c1" / "job.json", "{}");
    AssetCatalog& c = *lib.catalog;
    REQUIRE(c.idTaken("chair"));
    REQUIRE(c.idTaken("folder_only"));
    REQUIRE(c.idTaken("busy"));
    REQUIRE_FALSE(c.idTaken("lamp"));
    REQUIRE_FALSE(c.idTaken("bus"));
}

TEST_CASE("two catalogs on one library", "[core][catalog]") {
    TestLibrary lib("cat-two");
    Status status;
    auto second = AssetCatalog::open(lib.paths, status);
    plynth::test::requireOk(status);

    lib.add("chair", "Chair", {"chair"});
    REQUIRE(second->asset("chair"));
    plynth::test::requireOk(second->addCategory("rug", "Rug"));
    plynth::test::requireOk(lib.catalog->addRoomType("gaming", "Gaming room"));
    REQUIRE(second->roomTypeList().back().id == "gaming");
    REQUIRE(lib.catalog->categories().back().id == "rug");

    AssetInfo info = plynth::test::makeAssetOnDisk(lib.paths, "lamp", "Lamp");
    plynth::test::requireOk(second->indexAsset(info));
    AssetQuery query;
    query.text = "lamp";
    REQUIRE(lib.catalog->search(query).size() == 1);
}
