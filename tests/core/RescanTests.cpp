#include "CatalogSupport.h"

#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;
using plynth::test::TestLibrary;
using plynth::test::ids;

namespace {

// The folder names listed as broken.
std::vector<std::string> brokenFolders(const RescanReport& report)
{
    std::vector<std::string> out;
    for (const auto& [folder, status] : report.broken) out.push_back(folder);
    return out;
}

// Adds three assets that rescan sees as already indexed.
void addThree(TestLibrary& lib)
{
    lib.add("chair", "Chair", {"chair"}, {"oak"}, true);
    lib.add("lamp", "Lamp", {"lamp"});
    lib.add("sofa", "Sofa", {"sofa"});
}

}

TEST_CASE("rescan of an indexed library changes nothing", "[core][rescan]") {
    TestLibrary lib("rescan-same");
    addThree(lib);
    const RescanReport report = lib.catalog->rescan();
    REQUIRE(report.added == 0);
    REQUIRE(report.updated == 0);
    REQUIRE(report.removed == 0);
    REQUIRE(report.unchanged == 3);
    REQUIRE(report.needsRefresh.empty());
    REQUIRE(report.broken.empty());
}

TEST_CASE("a deleted assets.db is rebuilt with the same records", "[core][rescan]") {
    TestLibrary lib("rescan-lost-db");
    addThree(lib);
    plynth::test::requireOk(lib.catalog->addRoomType("gaming", "Gaming room"));
    plynth::test::requireOk(lib.catalog->addCategory("rug", "Rug"));
    lib.add("rugged", "Rugged", {"rug"});
    const std::vector<AssetRecord> before = lib.catalog->search({});
    lib.catalog.reset();

    fs::remove(lib.paths.assetsDb);
    REQUIRE(isLibraryFolder(lib.tmp.path() / "Library"));
    Status status;
    auto catalog = AssetCatalog::open(lib.paths, status);
    plynth::test::requireOk(status);
    REQUIRE(catalog->assetCount() == 0);

    const RescanReport report = catalog->rescan();
    REQUIRE(report.added == 4);
    REQUIRE(report.broken.empty());
    const std::vector<AssetRecord> after = catalog->search({});
    REQUIRE(after == before);

    for (const RoomType& t : catalog->roomTypeList()) REQUIRE(t.id != "gaming");
    REQUIRE(catalog->roomTypeList().size() == 9);
    bool rug = false;
    for (const CategoryEntry& c : catalog->categories()) rug = rug || (c.id == "rug" && c.assetCount == 1);
    REQUIRE(rug);
    REQUIRE(ids(catalog->search({.category = "chair"})) == std::vector<std::string>{"chair"});
}

TEST_CASE("a folder removed by hand drops its row", "[core][rescan]") {
    TestLibrary lib("rescan-removed");
    addThree(lib);
    fs::remove_all(lib.paths.assetItems / "lamp");
    const RescanReport report = lib.catalog->rescan();
    REQUIRE(report.removed == 1);
    REQUIRE(report.unchanged == 2);
    REQUIRE_FALSE(lib.catalog->asset("lamp"));
    AssetQuery query;
    query.text = "lamp";
    REQUIRE(lib.catalog->search(query).empty());
}

TEST_CASE("a folder with only asset.blend gets an info.json", "[core][rescan]") {
    TestLibrary lib("rescan-blend-only");
    plynth::test::writeBytes(lib.paths.assetItems / "red_lamp" / "asset.blend", "BLEND");
    const RescanReport report = lib.catalog->rescan();
    REQUIRE(report.added == 1);
    REQUIRE(report.needsRefresh == std::vector<std::string>{"red_lamp"});
    const fs::path file = lib.paths.assetItems / "red_lamp" / "info.json";
    REQUIRE(fs::exists(file));
    const AssetInfoRead read = readAssetInfo(file, "red_lamp");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.name == "Red lamp");
    REQUIRE(read.info.categories.empty());
    const auto record = lib.catalog->asset("red_lamp");
    REQUIRE(record);
    REQUIRE(record->uncategorized());
    REQUIRE(record->state == AssetState::NeedsRefresh);
    REQUIRE(ids(lib.catalog->search({.uncategorizedOnly = true})) == std::vector<std::string>{"red_lamp"});
}

TEST_CASE("a changed asset.blend is needs_refresh", "[core][rescan]") {
    TestLibrary lib("rescan-changed");
    addThree(lib);
    plynth::test::writeBytes(lib.paths.assetItems / "lamp" / "asset.blend", "A DIFFERENT SIZE");
    const RescanReport report = lib.catalog->rescan();
    REQUIRE(report.needsRefresh == std::vector<std::string>{"lamp"});
    REQUIRE(report.updated == 1);
    REQUIRE(report.unchanged == 2);
    REQUIRE(lib.catalog->asset("lamp")->state == AssetState::NeedsRefresh);
    const RescanReport again = lib.catalog->rescan();
    REQUIRE(again.needsRefresh == std::vector<std::string>{"lamp"});
    REQUIRE(again.unchanged == 3);
}

TEST_CASE("a different write time alone is needs_refresh", "[core][rescan]") {
    TestLibrary lib("rescan-mtime");
    addThree(lib);
    const fs::path blend = lib.paths.assetItems / "sofa" / "asset.blend";
    fs::last_write_time(blend, fs::last_write_time(blend) + std::chrono::hours(1));
    REQUIRE(lib.catalog->rescan().needsRefresh == std::vector<std::string>{"sofa"});
}

TEST_CASE("an info.json changed by hand is read again", "[core][rescan]") {
    TestLibrary lib("rescan-info-edit");
    addThree(lib);
    const fs::path file = lib.paths.assetItems / "lamp" / "info.json";
    AssetInfo info = readAssetInfo(file, "lamp").info;
    info.name = "Desk lamp";
    plynth::test::requireOk(writeAssetInfo(file, info));
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
    const RescanReport report = lib.catalog->rescan();
    REQUIRE(report.updated == 1);
    REQUIRE(lib.catalog->asset("lamp")->name == "Desk lamp");
}

TEST_CASE("a bad info.json is listed and the others still index", "[core][rescan]") {
    TestLibrary lib("rescan-broken");
    addThree(lib);
    plynth::test::writeBytes(lib.paths.assetItems / "old" / "asset.blend", "x");
    plynth::test::writeBytes(lib.paths.assetItems / "old" / "info.json", "{ not json");
    plynth::test::writeBytes(lib.paths.assetItems / "mismatch" / "asset.blend", "x");
    plynth::test::makeAssetOnDisk(lib.paths, "other_id", "Other");
    fs::rename(lib.paths.assetItems / "other_id" / "info.json", lib.paths.assetItems / "mismatch" / "info.json");
    fs::remove_all(lib.paths.assetItems / "other_id");
    fs::create_directories(lib.paths.assetItems / "Not A Valid Id");
    plynth::test::writeBytes(lib.paths.assetItems / "Not A Valid Id" / "asset.blend", "x");
    fs::create_directories(lib.paths.assetItems / "empty_folder");

    const RescanReport report = lib.catalog->rescan();
    auto broken = brokenFolders(report);
    std::sort(broken.begin(), broken.end());
    REQUIRE(broken == std::vector<std::string>{"Not A Valid Id", "empty_folder", "mismatch", "old"});
    for (const auto& [folder, status] : report.broken) {
        INFO(folder);
        REQUIRE(status.code != ErrorCode::None);
        if (folder == "old" || folder == "mismatch") REQUIRE(status.code == ErrorCode::BadFile);
    }
    REQUIRE(report.unchanged == 3);
    const auto old = lib.catalog->asset("old");
    REQUIRE(old);
    REQUIRE(old->state == AssetState::Broken);
    REQUIRE_FALSE(old->problem.empty());
    REQUIRE(lib.catalog->asset("chair")->state == AssetState::Ready);

    plynth::test::writeBytes(lib.paths.assetItems / "old" / "info.json", "{}");
    fs::remove_all(lib.paths.assetItems / "old");
    REQUIRE(lib.catalog->rescan().removed == 1);
}

TEST_CASE("rescan skips .staging and folders starting with a dot", "[core][rescan]") {
    TestLibrary lib("rescan-skip");
    addThree(lib);
    plynth::test::writeBytes(lib.paths.assetStaging / "chair-1234" / "asset.blend", "x");
    plynth::test::writeBytes(lib.paths.assetItems / ".hidden" / "asset.blend", "x");
    plynth::test::writeBytes(lib.paths.assetItems / "loose_file.txt", "x");
    const RescanReport report = lib.catalog->rescan();
    REQUIRE(report.added == 0);
    REQUIRE(report.broken.empty());
    REQUIRE(report.unchanged == 3);
    REQUIRE(lib.catalog->assetCount() == 3);
}

TEST_CASE("rescan counts the inbox", "[core][rescan]") {
    TestLibrary lib("rescan-inbox");
    plynth::test::writeBytes(lib.paths.assetInbox / "a.obj", "x");
    plynth::test::writeBytes(lib.paths.assetInbox / "b.zip", "x");
    plynth::test::writeBytes(lib.paths.assetInbox / "folder" / "c.fbx", "x");
    plynth::test::writeBytes(lib.paths.assetInbox / "~partial.obj", "x");
    plynth::test::writeBytes(lib.paths.assetInboxDone / "old.obj", "x");
    plynth::test::writeBytes(lib.paths.assetInboxFailed / "bad.obj", "x");
    REQUIRE(lib.catalog->rescan().inboxWaiting == 3);
}
