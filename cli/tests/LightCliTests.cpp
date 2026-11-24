#include "SampleProject.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

namespace {

// Plays the app: says this process has the project open with no unsaved changes.
void appOpens(const std::filesystem::path& folder) {
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(folder, state).ok());
}

}

TEST_CASE("light add places a ceiling light in a room at the usual height", "[cli][light]") {
    Sample sample("light-add");
    sample.drawRoomSquare();
    const auto added = sample.run({"light", "add", "--x", "3", "--y", "5"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("command") == "light add");
    const auto& light = added.answer.at("result");
    CHECK(light.at("id") == "l1");
    CHECK(light.at("floor") == "f1");
    CHECK(light.at("kind") == "ceiling");
    CHECK(light.at("room") == "r1");
    CHECK(light.at("x") == 3.0);
    CHECK(light.at("y") == 5.0);
    CHECK_THAT(light.at("z").get<double>(), WithinAbs(2.9, 1e-9));

    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    const auto& saved = plan.at("floors").at(0).at("lights");
    REQUIRE(saved.size() == 1);
    CHECK(saved.at(0).at("id") == "l1");
    CHECK(saved.at(0).at("room") == "r1");
}

TEST_CASE("light add takes a kind and a height, and a light outside every room has no room", "[cli][light]") {
    Sample sample("light-kinds");
    sample.drawRoomSquare();
    const auto wall = sample.run({"light", "add", "--x", "2", "--y", "4", "--kind", "wall"});
    REQUIRE(wall.exitCode == kExitOk);
    CHECK(wall.answer.at("result").at("kind") == "wall");
    CHECK_THAT(wall.answer.at("result").at("z").get<double>(), WithinAbs(1.8, 1e-9));
    const auto standing = sample.run({"light", "add", "--x", "4", "--y", "6", "--kind", "standing", "--z", "1.1"});
    REQUIRE(standing.exitCode == kExitOk);
    CHECK(standing.answer.at("result").at("kind") == "standing");
    CHECK(standing.answer.at("result").at("z") == 1.1);
    const auto outside = sample.run({"light", "add", "--x", "9", "--y", "9"});
    REQUIRE(outside.exitCode == kExitOk);
    CHECK(outside.answer.at("result").at("room").is_null());
    CHECK(outside.answer.at("result").at("id") == "l3");
}

TEST_CASE("light add refuses a bad kind, a bad height and an unknown floor and changes nothing", "[cli][light]") {
    Sample sample("light-add-refuse");
    sample.drawRoomSquare();
    const std::string before = readFile(sample.folder / "plan.json");

    const auto kind = sample.run({"light", "add", "--x", "3", "--y", "5", "--kind", "disco"});
    CHECK(kind.exitCode == kExitFailed);
    CHECK(errorCode(kind) == "invalid_value");
    const auto high = sample.run({"light", "add", "--x", "3", "--y", "5", "--z", "3.5"});
    CHECK(high.exitCode == kExitFailed);
    CHECK(errorCode(high) == "invalid_value");
    const auto low = sample.run({"light", "add", "--x", "3", "--y", "5", "--z=-0.1"});
    CHECK(errorCode(low) == "invalid_value");
    const auto floor = sample.run({"light", "add", "--x", "3", "--y", "5", "--floor", "f9"});
    CHECK(floor.exitCode == kExitFailed);
    CHECK(errorCode(floor) == "not_found");
    const auto missing = sample.run({"light", "add", "--x", "3"});
    CHECK(missing.exitCode == kExitBadArguments);
    CHECK(errorCode(missing) == "bad_arguments");
    const auto word = sample.run({"light", "add", "--x", "three", "--y", "5"});
    CHECK(errorCode(word) == "bad_arguments");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("light set moves a light into another room and changes its kind and height", "[cli][light]") {
    Sample sample("light-set");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light", "add", "--x", "9", "--y", "9"}).exitCode == kExitOk);

    const auto moved = sample.run({"light", "set", "l1", "--x", "3", "--y", "5"});
    REQUIRE(moved.exitCode == kExitOk);
    CHECK(moved.answer.at("command") == "light set");
    CHECK(moved.answer.at("result").at("x") == 3.0);
    CHECK(moved.answer.at("result").at("room") == "r1");
    CHECK(moved.answer.at("result").at("kind") == "ceiling");

    const auto kind = sample.run({"light", "set", "l1", "--kind", "wall"});
    REQUIRE(kind.exitCode == kExitOk);
    CHECK(kind.answer.at("result").at("kind") == "wall");
    CHECK_THAT(kind.answer.at("result").at("z").get<double>(), WithinAbs(2.9, 1e-9));
    CHECK(kind.answer.at("result").at("x") == 3.0);

    const auto all = sample.run({"light", "set", "l1", "--y", "4", "--z", "2.0", "--kind", "standing"});
    REQUIRE(all.exitCode == kExitOk);
    CHECK(all.answer.at("result").at("y") == 4.0);
    CHECK(all.answer.at("result").at("z") == 2.0);
    CHECK(all.answer.at("result").at("kind") == "standing");
}

TEST_CASE("light set refuses an unknown light, a bad value and no option, changing nothing", "[cli][light]") {
    Sample sample("light-set-refuse");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light", "add", "--x", "3", "--y", "5"}).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");

    CHECK(errorCode(sample.run({"light", "set", "l9", "--x", "1"})) == "not_found");
    CHECK(errorCode(sample.run({"light", "set", "w1", "--x", "1"})) == "not_found");
    CHECK(errorCode(sample.run({"light", "set", "l1", "--kind", "disco"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light", "set", "l1", "--z", "4"})) == "invalid_value");
    const auto group = sample.run({"light", "set", "l1", "--x", "4", "--z", "4"});
    CHECK(errorCode(group) == "invalid_value");
    const auto none = sample.run({"light", "set", "l1"});
    CHECK(none.exitCode == kExitBadArguments);
    CHECK(errorCode(none) == "bad_arguments");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("light remove and light list", "[cli][light]") {
    Sample sample("light-remove");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light", "add", "--x", "3", "--y", "5"}).exitCode == kExitOk);
    REQUIRE(sample.run({"light", "add", "--x", "4", "--y", "4", "--kind", "standing"}).exitCode == kExitOk);

    const auto listed = sample.run({"light", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    CHECK(listed.answer.at("result").at("floor") == "f1");
    const auto& lights = listed.answer.at("result").at("lights");
    REQUIRE(lights.size() == 2);
    CHECK(lights.at(0).at("id") == "l1");
    CHECK(lights.at(1).at("kind") == "standing");

    const auto removed = sample.run({"light", "remove", "l1"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == "l1");
    const auto after = sample.run({"light", "list"});
    REQUIRE(after.answer.at("result").at("lights").size() == 1);
    CHECK(after.answer.at("result").at("lights").at(0).at("id") == "l2");

    const std::string before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"light", "remove", "l1"})) == "not_found");
    CHECK(errorCode(sample.run({"light", "remove", "r1"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(errorCode(sample.run({"light", "list", "--floor", "f9"})) == "not_found");
}

TEST_CASE("light list shows only the lights of the floor asked for", "[cli][light]") {
    Sample sample("light-floors");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light", "add", "--x", "3", "--y", "5"}).exitCode == kExitOk);
    const auto floor = sample.run({"floor", "add"});
    REQUIRE(floor.exitCode == kExitOk);
    const std::string second = floor.answer.at("result").at("id");
    const auto upstairs = sample.run({"light", "add", "--x", "2", "--y", "2", "--floor", second});
    REQUIRE(upstairs.exitCode == kExitOk);
    CHECK(upstairs.answer.at("result").at("floor") == second);
    CHECK(upstairs.answer.at("result").at("room").is_null());

    const auto first = sample.run({"light", "list"});
    REQUIRE(first.answer.at("result").at("lights").size() == 1);
    const auto other = sample.run({"light", "list", "--floor", second});
    REQUIRE(other.answer.at("result").at("lights").size() == 1);
    CHECK(other.answer.at("result").at("lights").at(0).at("id") == upstairs.answer.at("result").at("id"));
}

TEST_CASE("the remove commands of other kinds point a light id to light remove", "[cli][light]") {
    Sample sample("light-other-remove");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light", "add", "--x", "3", "--y", "5"}).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");
    for (const char* word : {"item", "opening"}) {
        const auto refused = sample.run({word, "remove", "l1"});
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "invalid_value");
        CHECK(refused.answer.at("error").at("message").get<std::string>().find("light remove") != std::string::npos);
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("light edits follow the lock rules and light list never is refused", "[cli][light]") {
    Sample sample("light-lock");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light", "add", "--x", "3", "--y", "5"}).exitCode == kExitOk);
    appOpens(sample.folder);
    const std::string before = readFile(sample.folder / "plan.json");

    for (const auto& args : std::vector<std::vector<std::string>>{
             {"light", "add", "--x", "4", "--y", "4"}, {"light", "set", "l1", "--x", "4"}, {"light", "remove", "l1"}}) {
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(sample.run({"light", "list"}).exitCode == kExitOk);

    REQUIRE(sample.run({"session", "begin"}).exitCode == kExitOk);
    CHECK(sample.run({"light", "set", "l1", "--x", "4"}).exitCode == kExitOk);
    REQUIRE(sample.run({"session", "end"}).exitCode == kExitOk);
}

TEST_CASE("help lists the light commands", "[cli][light]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"light add", "light set", "light remove", "light list"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
    CHECK(plynth::cli::isCommandWord("light"));
}
