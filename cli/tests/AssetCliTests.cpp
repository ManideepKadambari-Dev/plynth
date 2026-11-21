#include "SampleProject.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::ids;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// Clears the settings file when a test starts and when it ends.
struct SettingsReset {
    SettingsReset() { clear(); }
    ~SettingsReset() { clear(); }
    static void clear() {
        std::error_code ignored;
        std::filesystem::remove(plynth::settingsFile(), ignored);
    }
};

// Writes one asset folder by hand and indexes it.
void makeAsset(const std::filesystem::path& library, const std::string& id, const std::string& name,
               const std::vector<std::string>& categories, const std::vector<std::string>& tags) {
    plynth::Status status;
    auto catalog = plynth::AssetCatalog::open(plynth::libraryPaths(library), status);
    REQUIRE(status.ok());
    const auto folder = catalog->paths().assetItems / id;
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "asset.blend", std::ios::binary) << "x";
    plynth::AssetInfo info;
    info.id = id;
    info.name = name;
    info.categories = categories;
    info.tags = tags;
    info.source.kind = "local_file";
    info.source.reference = id + ".obj";
    info.source.importedAt = plynth::sessionNow();
    info.size = {0.5, 0.5, 0.9};
    info.triangles = 12;
    info.blendBytes = 1;
    info.blenderVersion = "5.2.0";
    REQUIRE(plynth::writeAssetInfo(folder / "info.json", info).ok());
    REQUIRE(catalog->indexAsset(info).ok());
}

// A library made by the CLI and set in the settings, with a chair, an armchair and a sofa.
void makeLibrary(const TempFolder& temp) {
    const auto library = temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
    makeAsset(library, "chair", "Chair", {"chair"}, {"wood", "oak"});
    makeAsset(library, "armchair", "Arm chair", {"armchair"}, {"wood"});
    makeAsset(library, "sofa", "Sofa", {"sofa"}, {"fabric"});
}

}

TEST_CASE("library init with --use saves the library and library info describes it", "[cli][library]") {
    SettingsReset reset;
    TempFolder temp("library-init");
    const auto library = temp.path() / "Library";
    const auto init = runChecked({"library", "init", utf8(library), "--use"});
    REQUIRE(init.exitCode == kExitOk);
    const auto& result = init.answer.at("result");
    CHECK(result.at("assets").at("items") == 0);
    CHECK(result.at("assets").at("uncategorized") == 0);
    CHECK(result.at("materials").at("items") == 0);
    CHECK(result.at("free_bytes").get<unsigned long long>() > 0);

    const auto settings = runChecked({"settings", "show"});
    CHECK(settings.answer.at("result").at("settings").at("library") == utf8(library));

    const auto info = runChecked({"library", "info"});
    REQUIRE(info.exitCode == kExitOk);
    CHECK(info.answer.at("result").at("root") == utf8(library));

    const auto rescan = runChecked({"library", "rescan", "--no-blender"});
    REQUIRE(rescan.exitCode == kExitOk);
    CHECK(rescan.answer.at("result").at("assets").at("added") == 0);
}

TEST_CASE("assets commands without a library are no_library and name library init", "[cli][library]") {
    SettingsReset reset;
    const auto outcome = runChecked({"assets", "search", "cha"});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(errorCode(outcome) == "no_library");
    CHECK(outcome.answer.at("error").at("message").get<std::string>().find("library init") != std::string::npos);
}

TEST_CASE("assets search finds by words, room, category and tags", "[cli][assets]") {
    SettingsReset reset;
    TempFolder temp("assets-search");
    makeLibrary(temp);

    const auto cha = runChecked({"assets", "search", "cha"});
    REQUIRE(cha.exitCode == kExitOk);
    CHECK(cha.answer.at("result").at("count") == 2);
    const auto& first = cha.answer.at("result").at("assets").at(0);
    CHECK(first.at("size").at("height") == 0.9);
    CHECK(first.at("thumbnail").at("state") == "missing");

    const auto living = runChecked({"assets", "search", "--room", "living"});
    std::vector<std::string> found;
    for (const auto& asset : living.answer.at("result").at("assets")) {
        found.push_back(asset.at("id"));
    }
    CHECK(found == std::vector<std::string>{"armchair", "sofa"});

    const auto tags = runChecked({"assets", "list", "--tag", "wood", "--tag", "oak"});
    REQUIRE(tags.answer.at("result").at("count") == 1);
    CHECK(tags.answer.at("result").at("assets").at(0).at("id") == "chair");

    const auto badRoom = runChecked({"assets", "search", "--room", "nowhere"});
    CHECK(errorCode(badRoom) == "not_found");
}

TEST_CASE("assets set changes info.json and assets remove needs --yes", "[cli][assets]") {
    SettingsReset reset;
    TempFolder temp("assets-set");
    makeLibrary(temp);
    const auto folder = temp.path() / "Library" / "assets" / "items" / "chair";

    const auto set = runChecked({"assets", "set", "chair", "--add-category", "table", "--add-tag", "pine",
                                 "--remove-tag", "oak", "--name", "Oak chair"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("name") == "Oak chair");
    const std::string info = readFile(folder / "info.json");
    CHECK(info.find("\"table\"") != std::string::npos);
    CHECK(info.find("\"pine\"") != std::string::npos);
    CHECK(info.find("\"oak\"") == std::string::npos);

    const auto none = runChecked({"assets", "set", "chair"});
    CHECK(errorCode(none) == "bad_arguments");
    const auto unknown = runChecked({"assets", "set", "chair", "--add-category", "nothing"});
    CHECK(errorCode(unknown) == "not_found");

    const auto shown = runChecked({"assets", "info", "chair"});
    REQUIRE(shown.exitCode == kExitOk);
    CHECK(shown.answer.at("result").at("blender_version") == "5.2.0");
    CHECK(shown.answer.at("result").at("warnings").is_array());

    const auto refused = runChecked({"assets", "remove", "chair"});
    CHECK(errorCode(refused) == "bad_arguments");
    CHECK(std::filesystem::exists(folder));
    const auto removed = runChecked({"assets", "remove", "chair", "--yes"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == "chair");
    CHECK_FALSE(std::filesystem::exists(folder));
    CHECK(errorCode(runChecked({"assets", "info", "chair"})) == "not_found");
}

TEST_CASE("a room type added in the library is used by room types and room set", "[cli][assets]") {
    SettingsReset reset;
    TempFolder temp("assets-rooms");
    makeLibrary(temp);

    const auto rooms = runChecked({"assets", "rooms"});
    CHECK(rooms.answer.at("result").at("room_types").size() == 9);
    const auto added = runChecked({"assets", "room", "add", "gaming", "--name", "Gaming room"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("built_in") == false);
    const auto set = runChecked({"assets", "room", "set", "gaming", "--categories", "chair,lamp"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(ids(set.answer.at("result").at("categories")) == std::vector<std::string>{"chair", "lamp"});

    const auto types = runChecked({"room", "types"});
    bool listed = false;
    for (const auto& type : types.answer.at("result").at("room_types")) {
        listed = listed || type.at("id") == "gaming";
    }
    CHECK(listed);

    Sample sample("room-gaming");
    sample.drawRoomSquare();
    CHECK(sample.run({"room", "set", "r1", "--type", "gaming"}).exitCode == kExitOk);

    const auto removed = runChecked({"assets", "room", "remove", "gaming"});
    CHECK(removed.answer.at("result").at("removed") == "gaming");
    CHECK(sample.run({"room", "set", "r1", "--type", "gaming"}).exitCode != kExitOk);
}

TEST_CASE("category commands list the assets a removal changed", "[cli][assets]") {
    SettingsReset reset;
    TempFolder temp("assets-categories");
    makeLibrary(temp);

    const auto added = runChecked({"assets", "category", "add", "rug2", "--name", "Rug two"});
    REQUIRE(added.exitCode == kExitOk);
    const auto renamed = runChecked({"assets", "category", "set", "rug2", "--name", "Rug"});
    CHECK(renamed.answer.at("result").at("name") == "Rug");
    const auto removed = runChecked({"assets", "category", "remove", "sofa"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(ids(removed.answer.at("result").at("assets_changed")) == std::vector<std::string>{"sofa"});
    const auto sofa = runChecked({"assets", "info", "sofa"});
    CHECK(sofa.answer.at("result").at("categories").empty());
    const auto onDisk = nlohmann::json::parse(readFile(temp.path() / "Library" / "assets" / "items" / "sofa" / "info.json"));
    CHECK(onDisk.at("categories").empty());

    const auto categories = runChecked({"assets", "categories"});
    CHECK(categories.answer.at("result").at("categories").is_array());
    const auto licenses = runChecked({"assets", "licenses"});
    CHECK(licenses.answer.at("result").at("licenses").size() == 6);
    const auto importers = runChecked({"assets", "importers"});
    CHECK(importers.answer.at("result").at("importers").is_array());
}

TEST_CASE("a shape file in Library/shapes appears in opening shapes", "[cli][assets]") {
    SettingsReset reset;
    TempFolder temp("assets-shapes");
    std::filesystem::create_directories(temp.path() / "Library" / "shapes");
    std::ofstream(temp.path() / "Library" / "shapes" / "pentagon_t9.shape.json")
        << R"({"format":"plynth-shape","version":1,"id":"pentagon_t9","name":"Pentagon","outline":[[0,0],[1,0],[1,0.7],[0.5,1],[0,0.7]]})";

    makeLibrary(temp);
    const auto shapes = runChecked({"opening", "shapes"});
    REQUIRE(shapes.exitCode == kExitOk);
    CHECK(shapes.text.find("pentagon_t9") != std::string::npos);
}

TEST_CASE("asset commands refuse ids that are paths", "[cli][assets]") {
    SettingsReset reset;
    TempFolder temp("assets-bad-ids");
    makeLibrary(temp);
    const auto items = temp.path() / "Library" / "assets" / "items";
    const std::vector<std::string> bad = {"..", "../chair", R"(..\chair)","chair/..", "a/b", "C:/Windows", "Chair"};
    for (const std::string& id : bad) {
        INFO("id " << id);
        CHECK(errorCode(runChecked({"assets", "remove", id, "--yes"})) == "invalid_value");
        CHECK(errorCode(runChecked({"assets", "info", id})) == "invalid_value");
        CHECK(errorCode(runChecked({"assets", "set", id, "--name", "X"})) == "invalid_value");
        CHECK(errorCode(runChecked({"assets", "thumbnail", id})) == "invalid_value");
        CHECK(errorCode(runChecked({"assets", "room", "set", id, "--name", "X"})) == "invalid_value");
        CHECK(errorCode(runChecked({"assets", "room", "remove", id})) == "invalid_value");
        CHECK(errorCode(runChecked({"assets", "category", "set", id, "--name", "X"})) == "invalid_value");
        CHECK(errorCode(runChecked({"assets", "category", "remove", id})) == "invalid_value");
    }
    CHECK(errorCode(runChecked({"assets", "room", "add", "../x", "--name", "X"})) == "invalid_value");
    CHECK(errorCode(runChecked({"assets", "category", "add", "a/b", "--name", "X"})) == "invalid_value");
    CHECK(errorCode(runChecked({"assets", "import", "x.obj", "--id", "../evil"})) == "invalid_value");
    CHECK(std::filesystem::exists(items / "chair"));
    CHECK(std::filesystem::exists(items / "sofa"));
    CHECK(errorCode(runChecked({"assets", "remove", "nothing_here", "--yes"})) == "not_found");
}
