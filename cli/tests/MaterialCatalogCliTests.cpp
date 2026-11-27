#include "MaterialLibrary.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::ids;
using plynth::cli::testing::makeMaterial;
using plynth::cli::testing::MaterialLibrary;
using plynth::cli::testing::MaterialSettingsReset;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// The ids of the materials in an answer.
std::vector<std::string> foundIds(const plynth::cli::testing::Outcome& outcome) {
    std::vector<std::string> found;
    for (const auto& material : outcome.answer.at("result").at("materials")) {
        found.push_back(material.at("id"));
    }
    return found;
}

}

TEST_CASE("materials commands without a library are no_library", "[cli][materials]") {
    MaterialSettingsReset reset;
    for (const std::vector<std::string>& args :
         {std::vector<std::string>{"materials", "search", "oak"}, {"materials", "list"}, {"materials", "info", "oak"},
          {"materials", "groups"}, {"materials", "licenses"}, {"materials", "rescan"}}) {
        const auto outcome = runChecked(args);
        CHECK(outcome.exitCode != kExitOk);
        CHECK(errorCode(outcome) == "no_library");
    }
}

TEST_CASE("materials search finds by words, group, tags and license", "[cli][materials]") {
    MaterialLibrary library("materials-search");

    const auto all = runChecked({"materials", "list"});
    REQUIRE(all.exitCode == kExitOk);
    CHECK(all.answer.at("result").at("count") == 9);
    const auto& first = all.answer.at("result").at("materials").at(0);
    CHECK(first.at("tile_size").at("width") == 1.0);
    CHECK(first.at("state") == "ready");
    CHECK(first.at("problem").is_null());
    CHECK(first.at("thumbnail").at("state") == "ok");
    CHECK(first.at("source").at("kind") == "texture_set");

    CHECK(foundIds(runChecked({"materials", "search", "pai"})) == std::vector<std::string>{"painted_wood", "sage_paint", "white_paint"});
    CHECK(foundIds(runChecked({"materials", "list", "--group", "floor"})) ==
          std::vector<std::string>{"grey_tile", "oak_planks"});
    CHECK(foundIds(runChecked({"materials", "list", "--tag", "paint", "--license", "cc_by_4"})) ==
          std::vector<std::string>{"sage_paint"});
    CHECK(foundIds(runChecked({"materials", "list", "--uncategorized"})) == std::vector<std::string>{"stone_wall"});
    CHECK(foundIds(runChecked({"materials", "list", "--limit", "2"})).size() == 2);

    CHECK(errorCode(runChecked({"materials", "list", "--group", "nowhere"})) == "not_found");
    CHECK(errorCode(runChecked({"materials", "list", "--license", "nowhere"})) == "not_found");
    CHECK(errorCode(runChecked({"materials", "list", "--limit", "-1"})) == "invalid_value");
}

TEST_CASE("materials info lists the maps with absolute paths", "[cli][materials]") {
    MaterialLibrary library("materials-info");
    const auto info = runChecked({"materials", "info", "oak_planks"});
    REQUIRE(info.exitCode == kExitOk);
    const auto& result = info.answer.at("result");
    CHECK(result.at("texture_size").at("width") == 4);
    CHECK(result.at("warnings").is_array());
    const auto& maps = result.at("maps");
    CHECK(maps.size() == 6);
    const std::string normalText = maps.at("normal").at("file").get<std::string>();
    const std::filesystem::path normal{std::u8string(normalText.begin(), normalText.end())};
    CHECK(normal.is_absolute());
    CHECK(std::filesystem::exists(normal));
    CHECK(normal.filename() == "normal.png");
    CHECK(maps.at("normal").at("from") == "file");
    CHECK(maps.at("normal").at("detail") == "normal.png");
    CHECK(maps.at("opacity").at("file").is_null());
    CHECK(maps.at("opacity").at("from") == "none");
    CHECK(maps.at("occlusion").at("file").is_null());

    CHECK(errorCode(runChecked({"materials", "info", "gone"})) == "not_found");
    CHECK(errorCode(runChecked({"materials", "info", "../oak"})) == "invalid_value");
}

TEST_CASE("materials set edits info.json and a new tile size needs a refresh", "[cli][materials]") {
    MaterialLibrary library("materials-set");

    const auto set = runChecked({"materials", "set", "oak_planks", "--name", "Oak floor", "--group", "floor",
                                 "--group", "exterior", "--tag", "pine", "--license", "own_work"});
    REQUIRE(set.exitCode == kExitOk);
    const auto& result = set.answer.at("result");
    CHECK(result.at("name") == "Oak floor");
    CHECK(ids(result.at("groups")) == std::vector<std::string>{"floor", "exterior"});
    CHECK(ids(result.at("tags")) == std::vector<std::string>{"pine"});
    CHECK(result.at("license") == "own_work");
    CHECK(result.at("state") == "ready");
    const std::string info = readFile(library.item("oak_planks") / "info.json");
    CHECK(info.find("Oak floor") != std::string::npos);
    CHECK(info.find("\"pine\"") != std::string::npos);
    CHECK(info.find("\"oak\"") == std::string::npos);

    const auto tile = runChecked({"materials", "set", "oak_planks", "--tile", "2x1"});
    REQUIRE(tile.exitCode == kExitOk);
    CHECK(tile.answer.at("result").at("state") == "needs_refresh");
    CHECK(tile.answer.at("result").at("tile_size").at("width") == 2.0);
    CHECK(tile.answer.at("result").at("tile_size").at("height") == 1.0);

    const auto cleared = runChecked({"materials", "set", "oak_planks", "--no-groups"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("groups").empty());

    CHECK(errorCode(runChecked({"materials", "set", "oak_planks"})) == "bad_arguments");
    CHECK(errorCode(runChecked({"materials", "set", "oak_planks", "--group", "floor", "--no-groups"})) ==
          "bad_arguments");
    CHECK(errorCode(runChecked({"materials", "set", "oak_planks", "--tile", "0x1"})) == "invalid_value");
    CHECK(errorCode(runChecked({"materials", "set", "oak_planks", "--group", "nothing"})) == "not_found");
    CHECK(errorCode(runChecked({"materials", "set", "gone", "--name", "x"})) == "not_found");
    CHECK(runChecked({"materials", "set", "oak_planks", "--license", "nothing"}).exitCode != kExitOk);
}

TEST_CASE("materials remove needs --yes", "[cli][materials]") {
    MaterialLibrary library("materials-remove");
    const auto folder = library.item("stone_wall");
    const auto refused = runChecked({"materials", "remove", "stone_wall"});
    CHECK(errorCode(refused) == "bad_arguments");
    CHECK(std::filesystem::exists(folder));
    const auto removed = runChecked({"materials", "remove", "stone_wall", "--yes"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == "stone_wall");
    CHECK_FALSE(std::filesystem::exists(folder));
    CHECK(errorCode(runChecked({"materials", "info", "stone_wall"})) == "not_found");
    CHECK(errorCode(runChecked({"materials", "remove", "stone_wall", "--yes"})) == "not_found");
}

TEST_CASE("materials groups can be added, renamed and removed", "[cli][materials]") {
    MaterialLibrary library("materials-groups");

    const auto groups = runChecked({"materials", "groups"});
    REQUIRE(groups.exitCode == kExitOk);
    const auto& list = groups.answer.at("result").at("groups");
    CHECK(list.size() == 8);
    CHECK(list.at(0).at("id") == "floor");
    CHECK(list.at(0).at("built_in") == true);
    CHECK(list.at(0).at("materials") == 2);

    const auto added = runChecked({"materials", "group", "add", "fabric", "Fabric"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("name") == "Fabric");
    CHECK(added.answer.at("result").at("built_in") == false);
    CHECK(errorCode(runChecked({"materials", "group", "add", "fabric", "Again"})) == "already_exists");

    const auto renamed = runChecked({"materials", "group", "set", "fabric", "--name", "Cloth"});
    REQUIRE(renamed.exitCode == kExitOk);
    CHECK(renamed.answer.at("result").at("name") == "Cloth");
    CHECK(errorCode(runChecked({"materials", "group", "set", "fabric"})) == "bad_arguments");

    const auto before = runChecked({"materials", "groups"}).answer.at("result");
    const auto renameBuiltIn = runChecked({"materials", "group", "set", "floor", "--name", "X"});
    CHECK(errorCode(renameBuiltIn) == "invalid_value");
    CHECK(renameBuiltIn.answer.at("error").at("message") == "The built-in group Floor cannot be renamed.");
    const auto removeBuiltIn = runChecked({"materials", "group", "remove", "floor"});
    CHECK(errorCode(removeBuiltIn) == "invalid_value");
    CHECK(removeBuiltIn.answer.at("error").at("message") == "The built-in group Floor cannot be removed.");
    CHECK(runChecked({"materials", "groups"}).answer.at("result") == before);
    CHECK(readFile(library.item("oak_planks") / "info.json").find("\"floor\"") != std::string::npos);

    const auto removed = runChecked({"materials", "group", "remove", "fabric"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == "fabric");
    CHECK(errorCode(runChecked({"materials", "group", "remove", "fabric"})) == "not_found");
}

TEST_CASE("materials licenses lists the built in licenses", "[cli][materials]") {
    MaterialLibrary library("materials-licenses");
    const auto licenses = runChecked({"materials", "licenses"});
    REQUIRE(licenses.exitCode == kExitOk);
    CHECK(licenses.answer.at("result").at("licenses").size() >= 6);
    CHECK(licenses.answer.at("result").at("licenses").at(0).contains("redistributable"));
}

TEST_CASE("materials rescan reports changed and broken materials", "[cli][materials]") {
    MaterialLibrary library("materials-rescan");
    const auto quiet = runChecked({"materials", "rescan"});
    REQUIRE(quiet.exitCode == kExitOk);
    CHECK(quiet.answer.at("result").at("unchanged") == 9);
    CHECK(quiet.answer.at("result").at("needs_refresh").empty());
    CHECK(quiet.answer.at("result").at("broken").empty());

    std::ofstream(library.item("red_brick") / "textures" / "normal.png", std::ios::binary | std::ios::app)
        << "more bytes";
    std::filesystem::remove(library.item("oak_planks") / "textures" / "base_color.png");
    std::filesystem::create_directories(library.root / "materials" / "items" / "fresh");
    std::ofstream(library.root / "materials" / "items" / "fresh" / "a.png", std::ios::binary) << "x";

    const auto rescan = runChecked({"materials", "rescan", "--no-refresh"});
    REQUIRE(rescan.exitCode == kExitOk);
    const auto& result = rescan.answer.at("result");
    CHECK(ids(result.at("needs_refresh")) == std::vector<std::string>{"fresh", "red_brick"});
    CHECK(result.at("refreshed").empty());
    CHECK(result.at("refresh_failed").empty());
    CHECK(result.at("broken").size() == 1);
    CHECK(result.at("broken").at(0).at("folder") == "oak_planks");
    CHECK(result.at("added") == 1);
    CHECK(result.at("inbox_waiting") == 0);

    const auto broken = runChecked({"materials", "info", "oak_planks"});
    REQUIRE(broken.exitCode == kExitOk);
    CHECK(broken.answer.at("result").at("state") == "broken");
    CHECK(broken.answer.at("result").at("problem").is_string());
}

TEST_CASE("help lists the materials commands", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"materials search", "materials list", "materials info", "materials set",
                               "materials remove", "materials groups", "materials group add",
                               "materials group set", "materials group remove", "materials licenses",
                               "materials rescan"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
}

TEST_CASE("materials set --author saves the author", "[cli][materials][licenses]") {
    MaterialLibrary library("materials-author");
    const auto set = runChecked({"materials", "set", "oak_planks", "--author", "Studio A"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("author") == "Studio A");
    CHECK(readFile(library.item("oak_planks") / "info.json").find("Studio A") != std::string::npos);
    CHECK(errorCode(runChecked({"materials", "set", "oak_planks", "--author", std::string(121, 'b')})) == "invalid_value");
}
