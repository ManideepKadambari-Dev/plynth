#include "SampleProject.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::ids;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
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
               const std::string& category, plynth::AssetSize size) {
    plynth::Status status;
    auto catalog = plynth::AssetCatalog::open(plynth::libraryPaths(library), status);
    REQUIRE(status.ok());
    const auto folder = catalog->paths().assetItems / id;
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "asset.blend", std::ios::binary) << "x";
    plynth::AssetInfo info;
    info.id = id;
    info.name = name;
    info.categories = {category};
    info.source.kind = "local_file";
    info.source.reference = id + ".obj";
    info.source.importedAt = plynth::sessionNow();
    info.size = size;
    info.triangles = 12;
    info.blendBytes = 1;
    info.blenderVersion = "5.2.0";
    REQUIRE(plynth::writeAssetInfo(folder / "info.json", info).ok());
    REQUIRE(catalog->indexAsset(info).ok());
}

// A library made by the CLI and set in the settings, with a sofa, a table, a tv and an asset that is not measured.
std::filesystem::path makeLibrary(const Sample& sample) {
    const auto library = sample.temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
    makeAsset(library, "sofa", "Sofa", "sofa", {2.0, 0.9, 0.85});
    makeAsset(library, "table", "Table", "table", {1.2, 0.8, 0.75});
    makeAsset(library, "tv", "TV", "tv", {1.23, 0.06, 0.71});
    makeAsset(library, "blank", "Blank", "decor", {0.0, 0.0, 0.0});
    return library;
}

// Two 4 by 4 m rooms side by side with 0.2 m walls: the hall r1 (walls w1 to w4, x 1 to 5, y 1 to 5, w3 along the
// bottom) and the living room r2 (walls w5 to w8, x 7 to 11). The scale is 100 pixels per metre.
void buildFlat(const Sample& sample) {
    REQUIRE(sample.run({"scale", "set", "--pixels-per-metre", "100"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from-x", "1", "--from-y", "1", "--to-x", "5", "--to-y", "1"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from", "p2", "--to-x", "5", "--to-y", "5"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from", "p3", "--to-x", "1", "--to-y", "5"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from", "p4", "--to", "p1"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from-x", "7", "--from-y", "1", "--to-x", "11", "--to-y", "1"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from", "p6", "--to-x", "11", "--to-y", "5"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from", "p7", "--to-x", "7", "--to-y", "5"}).exitCode == kExitOk);
    REQUIRE(sample.draw({"--from", "p8", "--to", "p5"}).exitCode == kExitOk);
    REQUIRE(sample.run({"room", "add", "--x", "3", "--y", "3", "--type", "hall"}).exitCode == kExitOk);
    REQUIRE(sample.run({"room", "add", "--x", "9", "--y", "3", "--type", "living"}).exitCode == kExitOk);
}

// A flat with the library, ready for place commands.
void prepare(const Sample& sample) {
    makeLibrary(sample);
    buildFlat(sample);
}

// The warning codes of an answer.
std::vector<std::string> warningCodes(const plynth::cli::testing::Outcome& outcome) {
    std::vector<std::string> codes;
    if (outcome.answer.contains("warnings")) {
        for (const auto& warning : outcome.answer.at("warnings")) {
            codes.push_back(warning.at("code").get<std::string>());
        }
    }
    return codes;
}

// Says whether a code is among the warnings.
bool hasWarning(const plynth::cli::testing::Outcome& outcome, const std::string& code) {
    const auto codes = warningCodes(outcome);
    return std::ranges::find(codes, code) != codes.end();
}

}

TEST_CASE("place puts the design example sofa in the hall with its rotation", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-place");
    prepare(sample);
    const auto outcome = sample.run({"place", "sofa", "--room", "hall", "--x", "3.2", "--y", "3", "--rot", "90"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "place");
    const auto& item = outcome.answer.at("result").at("item");
    CHECK(item.at("id") == "i1");
    CHECK(item.at("floor") == "f1");
    CHECK(item.at("asset") == "sofa");
    CHECK(item.at("asset_name") == "Sofa");
    CHECK(item.at("asset_state") == "ok");
    CHECK(item.at("room") == "r1");
    CHECK_THAT(item.at("x").get<double>(), WithinAbs(3.2, 1e-9));
    CHECK_THAT(item.at("y").get<double>(), WithinAbs(3.0, 1e-9));
    CHECK(item.at("rotation") == 90.0);
    CHECK(item.at("wall").is_null());
    CHECK(item.at("wall_side").is_null());
    CHECK(item.at("wall_offset").is_null());
    CHECK_THAT(item.at("centre_height").get<double>(), WithinAbs(0.425, 1e-9));
    CHECK(item.at("size").at("width") == 2.0);
    CHECK(item.at("footprint").size() == 4);
    CHECK(item.at("problems").empty());
    CHECK_FALSE(outcome.answer.contains("warnings"));
}

TEST_CASE("place with only --room uses the label point of the room", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-label");
    prepare(sample);
    const auto outcome = sample.run({"place", "table", "--room", "r1"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& item = outcome.answer.at("result").at("item");
    CHECK(item.at("room") == "r1");
    CHECK_THAT(item.at("x").get<double>(), WithinAbs(3.0, 0.6));
    CHECK_THAT(item.at("y").get<double>(), WithinAbs(3.0, 0.6));

    const auto byType = sample.run({"place", "table", "--room", "living"});
    REQUIRE(byType.exitCode == kExitOk);
    CHECK(byType.answer.at("result").at("item").at("room") == "r2");
}

TEST_CASE("a room type shared by two rooms is invalid_value and names both", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-two-halls");
    prepare(sample);
    REQUIRE(sample.run({"room", "set", "r2", "--type", "hall"}).exitCode == kExitOk);
    const auto outcome = sample.run({"place", "sofa", "--room", "hall"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    const std::string message = outcome.answer.at("error").at("message");
    CHECK(message.find("r1") != std::string::npos);
    CHECK(message.find("r2") != std::string::npos);
}

TEST_CASE("place refuses before changing anything", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-refuse");
    prepare(sample);
    const std::string before = readFile(sample.folder / "plan.json");

    const auto outside = sample.run({"place", "sofa", "--room", "hall", "--x", "9", "--y", "3"});
    CHECK(outside.exitCode == kExitFailed);
    CHECK(errorCode(outside) == "invalid_value");

    const auto unknown = sample.run({"place", "nothing", "--x", "3", "--y", "3"});
    CHECK(unknown.exitCode == kExitFailed);
    CHECK(errorCode(unknown) == "not_found");

    const auto blank = sample.run({"place", "blank", "--x", "3", "--y", "3"});
    CHECK(blank.exitCode == kExitFailed);
    CHECK(errorCode(blank) == "invalid_value");

    const auto noRoom = sample.run({"place", "sofa", "--room", "garage"});
    CHECK(errorCode(noRoom) == "not_found");

    const auto noWall = sample.run({"place", "tv", "--wall", "w99"});
    CHECK(errorCode(noWall) == "not_found");

    const auto onlyX = sample.run({"place", "sofa", "--x", "3"});
    CHECK(onlyX.exitCode == kExitBadArguments);
    CHECK(errorCode(onlyX) == "bad_arguments");

    const auto nowhere = sample.run({"place", "sofa"});
    CHECK(nowhere.exitCode == kExitBadArguments);
    CHECK(errorCode(nowhere) == "bad_arguments");

    const auto both = sample.run({"place", "sofa", "--x", "3", "--y", "3", "--z", "0.1", "--centre", "0.5"});
    CHECK(errorCode(both) == "bad_arguments");

    const auto rotWall = sample.run({"place", "tv", "--wall", "w3", "--rot", "90"});
    CHECK(rotWall.exitCode == kExitBadArguments);
    CHECK(errorCode(rotWall) == "bad_arguments");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("an overlapping place succeeds with a warning and --strict refuses it", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-strict");
    prepare(sample);
    REQUIRE(sample.run({"place", "sofa", "--x", "3", "--y", "3"}).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");

    const auto strict = sample.run({"place", "table", "--x", "3", "--y", "3.2", "--strict"});
    CHECK(strict.exitCode == kExitFailed);
    CHECK(errorCode(strict) == "invalid_value");
    const std::string message = strict.answer.at("error").at("message");
    CHECK(message.find("overlaps") != std::string::npos);
    CHECK(message.find("in all.") != std::string::npos);
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto loose = sample.run({"place", "table", "--x", "3", "--y", "3.2"});
    REQUIRE(loose.exitCode == kExitOk);
    CHECK(hasWarning(loose, "overlaps_item"));
    const auto& problems = loose.answer.at("result").at("item").at("problems");
    REQUIRE_FALSE(problems.empty());
    CHECK(problems.at(0).at("code") == "overlaps_item");
    CHECK(problems.at(0).at("other") == "i1");
}

TEST_CASE("place --snap puts a sofa flush against a wall", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-snap");
    prepare(sample);
    const auto outcome = sample.run({"place", "sofa", "--x", "3", "--y", "4.4", "--snap", "0.3"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& item = outcome.answer.at("result").at("item");
    CHECK_THAT(item.at("y").get<double>(), WithinAbs(4.9 - 0.45, 1e-6));
}

TEST_CASE("place --wall mounts a tv at the default centre height", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-wall");
    prepare(sample);
    const auto outcome = sample.run({"place", "tv", "--wall", "w3", "--side", "left"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& item = outcome.answer.at("result").at("item");
    CHECK(item.at("wall") == "w3");
    CHECK(item.at("wall_side") == "left");
    CHECK_THAT(item.at("centre_height").get<double>(), WithinAbs(1.2, 1e-9));
    CHECK_THAT(item.at("wall_offset").get<double>(), WithinAbs(2.0, 1e-6));

    const auto inside = sample.run({"place", "tv", "--wall", "w3", "--room", "hall", "--offset", "1"});
    REQUIRE(inside.exitCode == kExitOk);
    const auto& facing = inside.answer.at("result").at("item");
    CHECK(facing.at("wall_side") == "right");
    CHECK(facing.at("room") == "r1");
    CHECK_THAT(facing.at("wall_offset").get<double>(), WithinAbs(1.0, 1e-6));

    const auto high = sample.run({"place", "tv", "--wall", "w3", "--room", "hall", "--centre", "1.5"});
    REQUIRE(high.exitCode == kExitOk);
    CHECK_THAT(high.answer.at("result").at("item").at("centre_height").get<double>(), WithinAbs(1.5, 1e-9));
}

TEST_CASE("item move slides a wall item and changes its height", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-move");
    prepare(sample);
    REQUIRE(sample.run({"place", "tv", "--wall", "w3", "--room", "hall"}).exitCode == kExitOk);
    REQUIRE(sample.run({"place", "table", "--x", "3", "--y", "3"}).exitCode == kExitOk);

    const auto slid = sample.run({"item", "move", "i1", "--offset", "1"});
    REQUIRE(slid.exitCode == kExitOk);
    CHECK_THAT(slid.answer.at("result").at("item").at("wall_offset").get<double>(), WithinAbs(1.0, 1e-6));

    const auto raised = sample.run({"item", "move", "i1", "--centre", "1.5"});
    REQUIRE(raised.exitCode == kExitOk);
    CHECK_THAT(raised.answer.at("result").at("item").at("centre_height").get<double>(), WithinAbs(1.5, 1e-9));

    const auto moved = sample.run({"item", "move", "i2", "--x", "2.5", "--y", "2.5"});
    REQUIRE(moved.exitCode == kExitOk);
    CHECK_THAT(moved.answer.at("result").at("item").at("x").get<double>(), WithinAbs(2.5, 1e-9));

    const auto lifted = sample.run({"item", "move", "i2", "--z", "0.2"});
    REQUIRE(lifted.exitCode == kExitOk);
    CHECK_THAT(lifted.answer.at("result").at("item").at("z").get<double>(), WithinAbs(0.2, 1e-9));

    const auto floorOffset = sample.run({"item", "move", "i2", "--offset", "1"});
    CHECK(floorOffset.exitCode == kExitBadArguments);
    CHECK(errorCode(floorOffset) == "bad_arguments");
    const auto none = sample.run({"item", "move", "i2"});
    CHECK(errorCode(none) == "bad_arguments");
    const auto unknown = sample.run({"item", "move", "i9", "--z", "0"});
    CHECK(errorCode(unknown) == "not_found");
}

TEST_CASE("item rotate turns by and to, and refuses a wall item", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-rotate");
    prepare(sample);
    REQUIRE(sample.run({"place", "sofa", "--x", "3", "--y", "3"}).exitCode == kExitOk);
    REQUIRE(sample.run({"place", "tv", "--wall", "w3", "--room", "hall"}).exitCode == kExitOk);

    const auto first = sample.run({"item", "rotate", "i1", "--by", "90"});
    REQUIRE(first.exitCode == kExitOk);
    CHECK(first.answer.at("result").at("item").at("rotation") == 90.0);
    const auto second = sample.run({"item", "rotate", "i1", "--by", "90"});
    CHECK(second.answer.at("result").at("item").at("rotation") == 180.0);
    const auto back = sample.run({"item", "rotate", "i1", "--to", "-90"});
    CHECK(back.answer.at("result").at("item").at("rotation") == 270.0);

    const auto wallItem = sample.run({"item", "rotate", "i2", "--by", "90"});
    CHECK(wallItem.exitCode == kExitFailed);
    CHECK(errorCode(wallItem) == "invalid_value");

    const auto neither = sample.run({"item", "rotate", "i1"});
    CHECK(errorCode(neither) == "bad_arguments");
    const auto both = sample.run({"item", "rotate", "i1", "--to", "1", "--by", "1"});
    CHECK(errorCode(both) == "bad_arguments");
}

TEST_CASE("item swap, attach and detach change one item", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-swap");
    prepare(sample);
    REQUIRE(sample.run({"place", "sofa", "--x", "3", "--y", "3"}).exitCode == kExitOk);

    const auto swapped = sample.run({"item", "swap", "i1", "table"});
    REQUIRE(swapped.exitCode == kExitOk);
    const auto& item = swapped.answer.at("result").at("item");
    CHECK(item.at("asset") == "table");
    CHECK(item.at("size").at("width") == 1.2);
    CHECK(sample.run({"item", "swap", "i1", "blank"}).exitCode == kExitFailed);
    CHECK(errorCode(sample.run({"item", "swap", "i1", "nothing"})) == "not_found");

    const auto attached = sample.run({"item", "attach", "i1", "--wall", "w3"});
    REQUIRE(attached.exitCode == kExitOk);
    CHECK(attached.answer.at("result").at("item").at("wall") == "w3");
    CHECK(attached.answer.at("result").at("item").at("wall_side") == "right");
    const double z = attached.answer.at("result").at("item").at("z");

    const auto detached = sample.run({"item", "detach", "i1"});
    REQUIRE(detached.exitCode == kExitOk);
    CHECK(detached.answer.at("result").at("item").at("wall").is_null());
    CHECK_THAT(detached.answer.at("result").at("item").at("z").get<double>(), WithinAbs(z, 1e-12));

    CHECK(errorCode(sample.run({"item", "attach", "i1", "--wall", "w99"})) == "not_found");
    CHECK(errorCode(sample.run({"item", "attach", "i9", "--wall", "w3"})) == "not_found");
}

TEST_CASE("item list works without a library and shows missing assets with one", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-list");
    const auto library = makeLibrary(sample);
    buildFlat(sample);
    REQUIRE(sample.run({"place", "sofa", "--room", "hall", "--x", "3", "--y", "3"}).exitCode == kExitOk);
    REQUIRE(sample.run({"place", "tv", "--wall", "w3", "--room", "hall"}).exitCode == kExitOk);

    const auto withLibrary = sample.run({"item", "list"});
    REQUIRE(withLibrary.exitCode == kExitOk);
    CHECK(withLibrary.answer.at("result").at("count") == 2);
    CHECK(withLibrary.answer.at("result").at("items").at(0).at("asset_state") == "ok");

    const auto onWall = sample.run({"item", "list", "--wall", "w3"});
    CHECK(onWall.answer.at("result").at("count") == 1);
    const auto inRoom = sample.run({"item", "list", "--room", "r1"});
    CHECK(inRoom.answer.at("result").at("count") == 2);
    CHECK(errorCode(sample.run({"item", "list", "--room", "r9"})) == "not_found");

    std::filesystem::remove_all(library / "assets" / "items" / "sofa");
    REQUIRE(runChecked({"library", "rescan", "--no-blender"}).exitCode == kExitOk);
    const auto missing = sample.run({"item", "list"});
    REQUIRE(missing.exitCode == kExitOk);
    const auto& first = missing.answer.at("result").at("items").at(0);
    CHECK(first.at("asset_state") == "missing");
    CHECK(first.at("asset_name").is_null());
    const auto& problems = first.at("problems");
    REQUIRE_FALSE(problems.empty());
    CHECK(problems.at(0).at("code") == "missing_asset");

    const auto moved = sample.run({"item", "move", "i1", "--x", "3.5", "--y", "3"});
    CHECK(moved.exitCode == kExitOk);
    CHECK(hasWarning(moved, "missing_asset"));

    SettingsReset::clear();
    const auto unknown = sample.run({"item", "list"});
    REQUIRE(unknown.exitCode == kExitOk);
    const auto& entry = unknown.answer.at("result").at("items").at(0);
    CHECK(entry.at("asset_state") == "unknown");
    CHECK(entry.at("asset_name").is_null());
    for (const auto& problem : entry.at("problems")) {
        CHECK(problem.at("code") != "missing_asset");
    }
}

TEST_CASE("item remove and wall remove list what they took", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-remove");
    prepare(sample);
    REQUIRE(sample.run({"place", "sofa", "--x", "3", "--y", "3"}).exitCode == kExitOk);
    REQUIRE(sample.run({"place", "tv", "--wall", "w3", "--room", "hall"}).exitCode == kExitOk);

    const auto removed = sample.run({"item", "remove", "i1"});
    REQUIRE(removed.exitCode == kExitOk);
    const auto& list = removed.answer.at("result").at("removed");
    CHECK(ids(list.at("items")) == std::vector<std::string>{"i1"});
    CHECK(list.at("detached").empty());
    CHECK(errorCode(sample.run({"item", "remove", "i1"})) == "not_found");
    CHECK(errorCode(sample.run({"wall", "remove", "i2", "--cascade"})) == "invalid_value");

    const auto blocked = sample.run({"wall", "remove", "w3"});
    CHECK(blocked.exitCode == kExitFailed);
    CHECK(errorCode(blocked) == "in_use");

    const auto cascade = sample.run({"wall", "remove", "w3", "--cascade"});
    REQUIRE(cascade.exitCode == kExitOk);
    const auto& wallList = cascade.answer.at("result").at("removed");
    CHECK(ids(wallList.at("detached")) == std::vector<std::string>{"i2"});
    CHECK(ids(wallList.at("walls")) == std::vector<std::string>{"w3"});
    const auto items = sample.run({"item", "list"});
    CHECK(items.answer.at("result").at("count") == 1);
    CHECK(items.answer.at("result").at("items").at(0).at("wall").is_null());
}

TEST_CASE("the saved plan with items matches its schema", "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-schema");
    prepare(sample);
    REQUIRE(sample.run({"place", "sofa", "--room", "hall", "--x", "3", "--y", "3", "--rot", "90"}).exitCode == kExitOk);
    REQUIRE(sample.run({"place", "tv", "--wall", "w3", "--room", "hall"}).exitCode == kExitOk);

    std::ifstream schemaFile(std::filesystem::path(PLYNTH_SCHEMA_DIR) / "plan.schema.json");
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schemaFile));
    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    CHECK_NOTHROW(validator.validate(plan));
    CHECK(plan.at("version") == 3);
    CHECK(plan.at("floors").at(0).at("items").size() == 2);
}

TEST_CASE("item edits are refused with locked while the app has the project open and the list is not",
          "[cli][item]") {
    SettingsReset reset;
    const Sample sample("item-locked");
    prepare(sample);
    REQUIRE(sample.run({"place", "sofa", "--x", "3", "--y", "3"}).exitCode == kExitOk);
    REQUIRE(sample.run({"place", "tv", "--wall", "w3", "--room", "hall"}).exitCode == kExitOk);
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const std::string before = readFile(sample.folder / "plan.json");

    const std::vector<std::vector<std::string>> edits = {
        {"place", "table", "--x", "3", "--y", "2"},
        {"item", "move", "i1", "--x", "2", "--y", "2"},
        {"item", "rotate", "i1", "--by", "90"},
        {"item", "swap", "i1", "table"},
        {"item", "attach", "i1", "--wall", "w3"},
        {"item", "detach", "i2"},
        {"item", "remove", "i1"},
    };
    for (const auto& args : edits) {
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(sample.run({"item", "list"}).exitCode == kExitOk);
}
