#include "PackSupportTests.h"

#include <plynth/core/catalog/Integrations.h>
#include <plynth/core/BuildResolve.h>
#include <plynth/core/catalog/BuildChoices.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// Every file below a folder, relative, sorted. Empty when the folder is missing.
std::vector<std::string> filesBelow(const fs::path& folder)
{
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::exists(folder, ec)) return files;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (entry.is_regular_file()) files.push_back(pathToUtf8(entry.path().lexically_relative(folder)));
    }
    std::sort(files.begin(), files.end());
    return files;
}

// The items of library B as they are on disk, to check that a refused unpack changed nothing.
std::vector<std::string> libraryState(const PackFixture& f)
{
    std::vector<std::string> state = filesBelow(f.b.assetItems);
    for (const std::string& file : filesBelow(f.b.materialItems)) state.push_back("materials/" + file);
    for (const std::string& file : filesBelow(f.b.shapes)) state.push_back("shapes/" + file);
    return state;
}

// The action unpack chose for an id.
UnpackAction actionOf(const std::vector<UnpackEntry>& entries, const std::string& id)
{
    for (const UnpackEntry& entry : entries) {
        if (entry.entry.id == id) return entry.action;
    }
    FAIL_CHECK("no entry " << id);
    return UnpackAction::Missing;
}

// A packed project with an own asset, a store asset, an own material, a shape and a room type.
void packSample(PackFixture& f)
{
    f.asset("sofa");
    f.asset("chair", "blenderkit", "royalty_free", "https://www.blenderkit.com/get/abc");
    f.material("brick");
    f.shape("arch");
    requireOk(f.pack({{PackKind::Asset, "sofa"}, {PackKind::Asset, "chair"}, {PackKind::Material, "brick"},
                      {PackKind::Shape, "arch"}}, false, {{"gaming", "Gaming room"}}).status);
}

}

TEST_CASE("unpack into an empty library adds the copied items and indexes them", "[unpack]") {
    PackFixture f("unpack-empty");
    packSample(f);
    const UnpackReport report = unpackProject(f.project, f.b, {});
    requireOk(report.status);
    CHECK(report.added == 3);
    CHECK_FALSE(report.complete);
    CHECK(actionOf(report.entries, "sofa") == UnpackAction::Add);
    CHECK(actionOf(report.entries, "brick") == UnpackAction::Add);
    CHECK(actionOf(report.entries, "arch") == UnpackAction::Add);
    CHECK(actionOf(report.entries, "chair") == UnpackAction::NeedsDownload);
    for (const UnpackEntry& entry : report.entries) {
        if (entry.entry.id == "chair") CHECK(entry.command == "plynth assets import \"https://www.blenderkit.com/get/abc\"");
    }

    Status status;
    auto assets = AssetCatalog::open(f.b, status);
    requireOk(status);
    CHECK(assets->asset("sofa").has_value());
    CHECK_FALSE(assets->asset("chair").has_value());
    bool gaming = false;
    for (const RoomType& room : assets->roomTypeList()) gaming = gaming || room.id == "gaming";
    CHECK(gaming);
    auto materials = MaterialCatalog::open(f.b, status);
    requireOk(status);
    CHECK(materials->material("brick").has_value());
    CHECK(fs::exists(f.b.shapes / "arch.shape.json"));
    CHECK(fs::last_write_time(f.b.assetItems / "sofa" / "asset.blend") ==
          fs::last_write_time(f.a.assetItems / "sofa" / "asset.blend"));

    const RescanReport rescan = assets->rescan();
    CHECK(rescan.needsRefresh.empty());
    CHECK(rescan.broken.empty());
    CHECK(filesBelow(f.b.assetStaging).empty());
    CHECK(filesBelow(f.b.materialStaging).empty());
}

TEST_CASE("a second unpack finds everything present and adds nothing", "[unpack]") {
    PackFixture f("unpack-twice");
    packSample(f);
    requireOk(unpackProject(f.project, f.b, {}).status);
    const UnpackReport again = unpackProject(f.project, f.b, {});
    requireOk(again.status);
    CHECK(again.added == 0);
    CHECK(actionOf(again.entries, "sofa") == UnpackAction::Present);
    CHECK(actionOf(again.entries, "brick") == UnpackAction::Present);
    CHECK(actionOf(again.entries, "arch") == UnpackAction::Present);
}

TEST_CASE("a conflict writes nothing unless the library's items are kept", "[unpack]") {
    PackFixture f("unpack-conflict");
    packSample(f);
    writeBytes(f.b.assetItems / "sofa" / "asset.blend", "A DIFFERENT SOFA");
    writeBytes(f.b.assetItems / "sofa" / "info.json", "{}");
    writeBytes(f.b.shapes / "arch.shape.json", "{\"different\": true}");
    const std::vector<std::string> before = libraryState(f);

    const UnpackReport refused = unpackProject(f.project, f.b, {});
    CHECK(refused.status.code == ErrorCode::AlreadyExists);
    CHECK(refused.status.message.find("sofa") != std::string::npos);
    CHECK(refused.status.message.find("arch") != std::string::npos);
    CHECK(refused.added == 0);
    CHECK(libraryState(f) == before);

    UnpackOptions keep;
    keep.keepLibrary = true;
    const UnpackReport kept = unpackProject(f.project, f.b, keep);
    requireOk(kept.status);
    CHECK(kept.added == 1);
    CHECK(actionOf(kept.entries, "sofa") == UnpackAction::Conflict);
    CHECK(actionOf(kept.entries, "brick") == UnpackAction::Add);
    CHECK(readTextFile(f.b.assetItems / "sofa" / "asset.blend") == "A DIFFERENT SOFA");
}

TEST_CASE("a reference is present when the library has the same source, otherwise a conflict", "[unpack]") {
    PackFixture f("unpack-reference");
    packSample(f);
    AssetInfo same = makeAssetOnDisk(f.b, "chair", "Chair");
    same.source = {"blenderkit", "https://www.blenderkit.com/get/abc", sessionNow()};
    requireOk(writeAssetInfo(f.b.assetItems / "chair" / "info.json", same));
    CHECK(actionOf(planUnpack(f.project, f.b).entries, "chair") == UnpackAction::Present);

    same.source.reference = "https://www.blenderkit.com/get/other";
    requireOk(writeAssetInfo(f.b.assetItems / "chair" / "info.json", same));
    CHECK(actionOf(planUnpack(f.project, f.b).entries, "chair") == UnpackAction::Conflict);
}

TEST_CASE("a stopped unpack adds nothing", "[unpack]") {
    PackFixture f("unpack-stop");
    packSample(f);
    const std::vector<std::string> before = libraryState(f);
    std::stop_source stop;
    stop.request_stop();
    const UnpackReport report = unpackProject(f.project, f.b, {}, stop.get_token());
    CHECK(report.status.code == ErrorCode::Cancelled);
    CHECK(report.added == 0);
    CHECK(libraryState(f) == before);
    CHECK(filesBelow(f.b.assetStaging).empty());
}

TEST_CASE("unpack without a pack or a library says why", "[unpack]") {
    PackFixture f("unpack-nothing");
    CHECK(unpackProject(f.project, f.b, {}).status.code == ErrorCode::NotFound);
    packSample(f);
    CHECK(unpackProject(f.project, libraryPaths(f.tmp.path() / "nolibrary"), {}).status.code == ErrorCode::NoLibrary);
}

TEST_CASE("hostile packs are refused before anything is written", "[unpack]") {
    using Change = std::function<void(PackFixture&, nlohmann::json&)>;
    const auto entryOf = [](nlohmann::json& json, const std::string& id) -> nlohmann::json& {
        for (auto& entry : json["entries"]) {
            if (entry["id"] == id) return entry;
        }
        throw std::runtime_error("no entry " + id);
    };
    const std::vector<std::pair<std::string, Change>> cases = {
        {"a newer version", [](PackFixture&, nlohmann::json& j) { j["version"] = 2; }},
        {"another format", [](PackFixture&, nlohmann::json& j) { j["format"] = "something-else"; }},
        {"a path that climbs out", [&](PackFixture& f, nlohmann::json& j) {
             entryOf(j, "sofa")["files"].push_back("assets/sofa/../../../evil.png");
             writeBytes(f.project / "evil.png", "X");
         }},
        {"an absolute path", [&](PackFixture&, nlohmann::json& j) { entryOf(j, "sofa")["files"].push_back("C:/Windows/evil.png"); }},
        {"a UNC path", [&](PackFixture&, nlohmann::json& j) { entryOf(j, "sofa")["files"].push_back("//server/share/evil.png"); }},
        {"a reserved name", [&](PackFixture&, nlohmann::json& j) {
             entryOf(j, "sofa")["files"].push_back("assets/sofa/textures/AUX.png");
         }},
        {"a file outside its entry", [&](PackFixture& f, nlohmann::json& j) {
             entryOf(j, "sofa")["files"].push_back("materials/brick/extra.png");
             writeBytes(f.packFolder() / "materials" / "brick" / "extra.png", "X");
         }},
        {"an unexpected file type", [&](PackFixture& f, nlohmann::json& j) {
             entryOf(j, "sofa")["files"].push_back("assets/sofa/run.exe");
             writeBytes(f.packFolder() / "assets" / "sofa" / "run.exe", "MZ");
         }},
        {"an unlisted file", [](PackFixture& f, nlohmann::json&) {
             writeBytes(f.packFolder() / "assets" / "sofa" / "textures" / "hidden.png", "X");
         }},
        {"a listed file that is missing", [](PackFixture& f, nlohmann::json&) {
             fs::remove(f.packFolder() / "assets" / "sofa" / "textures" / "wood.png");
         }},
        {"a store item passed off as copied", [&](PackFixture& f, nlohmann::json& j) {
             auto& chair = entryOf(j, "chair");
             chair["state"] = "copied";
             chair.erase("reason");
             chair["files"] = {"assets/chair/info.json", "assets/chair/asset.blend"};
             writeBytes(f.packFolder() / "assets" / "chair" / "info.json", readTextFile(f.a.assetItems / "chair" / "info.json"));
             writeBytes(f.packFolder() / "assets" / "chair" / "asset.blend", "FAKEBLEND");
         }},
        {"an info.json that says it came from a store", [&](PackFixture& f, nlohmann::json&) {
             const fs::path info = f.packFolder() / "assets" / "sofa" / "info.json";
             nlohmann::json changed = nlohmann::json::parse(readTextFile(info));
             changed["source"]["kind"] = "blenderkit";
             writeBytes(info, changed.dump(2));
         }},
        {"an info.json its reader refuses", [](PackFixture& f, nlohmann::json&) {
             writeBytes(f.packFolder() / "assets" / "sofa" / "info.json", "{\"format\": \"plynth-asset\"}");
         }},
        {"a broken shape", [](PackFixture& f, nlohmann::json&) {
             writeBytes(f.packFolder() / "shapes" / "arch.shape.json", "{\"format\": \"plynth-shape\"}");
         }},
        {"an entry listed twice", [&](PackFixture&, nlohmann::json& j) { j["entries"].push_back(entryOf(j, "brick")); }},
        {"an unknown field", [](PackFixture&, nlohmann::json& j) { j["run"] = "evil.bat"; }},
    };
    for (const auto& [name, change] : cases) {
        DYNAMIC_SECTION(name) {
            PackFixture f("unpack-hostile");
            packSample(f);
            const std::vector<std::string> before = libraryState(f);
            nlohmann::json json = f.manifest();
            change(f, json);
            f.writeManifest(json);
            const UnpackReport report = unpackProject(f.project, f.b, {});
            INFO(report.status.message);
            CHECK_FALSE(report.status.ok());
            CHECK((report.status.code == ErrorCode::BadFile || report.status.code == ErrorCode::UnsupportedVersion));
            CHECK(libraryState(f) == before);
            CHECK(report.added == 0);
        }
    }
}

TEST_CASE("a linked folder inside a pack is refused", "[unpack]") {
    PackFixture f("unpack-link");
    packSample(f);
    const std::vector<std::string> before = libraryState(f);
    std::error_code ec;
    fs::create_directory_symlink(f.tmp.path(), f.packFolder() / "assets" / "elsewhere", ec);
    if (ec) {
        SKIP("This machine does not allow links to be made.");
    }
    const UnpackReport report = unpackProject(f.project, f.b, {});
    CHECK(report.status.code == ErrorCode::BadFile);
    CHECK(report.status.message.find("link") != std::string::npos);
    CHECK(libraryState(f) == before);
}

TEST_CASE("the pack and unpack answers have their fields", "[unpack]") {
    PackFixture f("unpack-json");
    f.asset("sofa");
    f.asset("chair", "blenderkit", "cc0", "https://www.blenderkit.com/get/abc");
    const PackReport packed = f.pack({{PackKind::Asset, "sofa"}, {PackKind::Asset, "chair"}});
    requireOk(packed.status);
    const Json json = packReportToJson(packed);
    CHECK(json.at("copied") == 1);
    CHECK(json.at("references") == 1);
    CHECK(json.at("missing") == 0);
    CHECK(json.at("complete") == true);
    CHECK(json.at("folder").get<std::string>().find('\\') == std::string::npos);
    CHECK(json.at("entries").at(1).at("reason") == "store");

    const UnpackReport unpacked = unpackProject(f.project, f.b, {});
    requireOk(unpacked.status);
    const Json out = unpackReportToJson(unpacked);
    CHECK(out.at("added") == 1);
    CHECK(out.at("complete") == false);
    CHECK(out.at("entries").at(0).at("action") == "add");
    CHECK(out.at("entries").at(1).at("action") == "needs_download");
    CHECK(out.at("entries").at(1).contains("command"));
}

TEST_CASE("a Fab item is only named and unpack names the importer to bring it back", "[unpack]") {
    PackFixture f("unpack-fab");
    f.asset("table", "fab_zip", "cc0", "Table_pack.zip");
    f.material("oak", "cc0", "fab_zip");
    requireOk(f.pack({{PackKind::Asset, "table"}, {PackKind::Material, "oak"}}).status);
    const PackRead read = readPack(f.project);
    requireOk(read.status);
    CHECK(findEntry(read.manifest, "table")->reason == PackReason::Store);
    CHECK(findEntry(read.manifest, "oak")->reason == PackReason::Store);

    const UnpackPlan plan = planUnpack(f.project, f.b);
    requireOk(plan.status);
    for (const UnpackEntry& entry : plan.entries) {
        INFO(entry.entry.id);
        CHECK(entry.action == UnpackAction::NeedsDownload);
        if (entry.entry.id == "table") CHECK(entry.command == "plynth assets import \"Table_pack.zip\" --importer fab_zip");
        if (entry.entry.id == "oak") CHECK(entry.command.rfind("plynth materials import \"", 0) == 0);
    }
    CHECK(isStoreSource("fab_zip"));
    CHECK(isStoreSource("blenderkit"));
    CHECK_FALSE(isStoreSource("local_file"));
}

TEST_CASE("a project unpacked into a library with the same candidates resolves to the same build", "[unpack][resolved]") {
    PackFixture f("unpack-resolved");
    Project project = squareProjectWithWalls();
    int n = 0;
    for (const char* id : {"sofa_a", "sofa_b", "sofa_c"}) {
        AssetInfo info = f.asset(id);
        info.categories = {"sofa"};
        requireOk(writeAssetInfo(f.a.assetItems / id / "info.json", info));
        requireOk(f.assets->indexAsset(info));
        Item item;
        item.id = "i" + std::to_string(++n);
        item.asset = id;
        item.x = n;
        item.y = 2;
        item.size = {info.size.width, info.size.depth, info.size.height};
        project.plan.floors[0].items.push_back(item);
    }
    const PackContents contents = packContents(project, f.a);
    requireOk(f.pack(contents.used).status);
    const UnpackReport unpacked = unpackProject(f.project, f.b, {});
    requireOk(unpacked.status);

    Status status;
    const auto assetsB = AssetCatalog::open(f.b, status);
    requireOk(status);
    const auto materialsB = MaterialCatalog::open(f.b, status);
    requireOk(status);
    const BuildChoices choicesA = buildChoicesFrom(f.assets.get(), f.materials.get());
    const BuildChoices choicesB = buildChoicesFrom(assetsB.get(), materialsB.get());
    REQUIRE(choicesA.assets.size() == 3);
    REQUIRE(choicesB.assets == choicesA.assets);
    for (std::uint32_t seed : {0u, 1u, 7u, 12u}) {
        INFO("seed " << seed);
        project.settings.variation.seed = seed;
        const ResolvedBuild here = resolveBuild(project, choicesA);
        const ResolvedBuild there = resolveBuild(project, choicesB);
        CHECK(here.project == there.project);
        CHECK(here.changes == there.changes);
        CHECK(here.dressing == there.dressing);
        CHECK(here.frames == there.frames);
    }
}
