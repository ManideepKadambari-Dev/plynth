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

// Adds a floor above and the stair s1 on the first floor, for step strips.
void addStair(const Sample& sample) {
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"stair", "add", "--floor", "f1", "--x", "2", "--y", "4"}).exitCode == kExitOk);
}

}

TEST_CASE("light-strip add puts a cove strip along a line at the usual height", "[cli][light-strip]") {
    Sample sample("strip-add");
    sample.drawRoomSquare();
    const auto added = sample.run({"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2,4.8,6.8"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("command") == "light-strip add");
    const auto& strip = added.answer.at("result");
    CHECK(strip.at("id") == "ls1");
    CHECK(strip.at("floor") == "f1");
    CHECK(strip.at("kind") == "cove");
    REQUIRE(strip.at("points").size() == 3);
    CHECK(strip.at("points").at(0).at("x") == 1.2);
    CHECK(strip.at("points").at(2).at("y") == 6.8);
    CHECK_THAT(strip.at("z").get<double>(), WithinAbs(2.95, 1e-9));
    CHECK(strip.at("stair").is_null());
    CHECK(strip.at("power") == 10.0);

    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    const auto& saved = plan.at("floors").at(0).at("light_strips");
    REQUIRE(saved.size() == 1);
    CHECK(saved.at(0).at("id") == "ls1");
    CHECK(saved.at(0).at("kind") == "cove");
    CHECK(saved.at(0).at("points").size() == 3);
}

TEST_CASE("light-strip add takes every kind with its height, power and stair", "[cli][light-strip]") {
    Sample sample("strip-kinds");
    sample.drawRoomSquare();
    addStair(sample);

    const auto soffit = sample.run({"light-strip", "add", "--kind", "soffit", "--points", "1.5,4,3,4"});
    REQUIRE(soffit.exitCode == kExitOk);
    CHECK_THAT(soffit.answer.at("result").at("z").get<double>(), WithinAbs(3.0, 1e-9));

    const auto edge =
        sample.run({"light-strip", "add", "--kind", "edge", "--points", "1,7,5,7", "--z", "1", "--power", "20"});
    REQUIRE(edge.exitCode == kExitOk);
    CHECK(edge.answer.at("result").at("kind") == "edge");
    CHECK(edge.answer.at("result").at("z") == 1.0);
    CHECK(edge.answer.at("result").at("power") == 20.0);

    const auto step = sample.run({"light-strip", "add", "--kind", "step", "--stair", "s1"});
    REQUIRE(step.exitCode == kExitOk);
    CHECK(step.answer.at("result").at("id") == "ls3");
    CHECK(step.answer.at("result").at("stair") == "s1");
    CHECK(step.answer.at("result").at("points").empty());
    CHECK(step.answer.at("result").at("z") == 0.0);

    const auto upstairs = sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,1,2,1", "--floor", "f2"});
    REQUIRE(upstairs.exitCode == kExitOk);
    CHECK(upstairs.answer.at("result").at("floor") == "f2");
}

TEST_CASE("light-strip add refuses bad values before changing anything", "[cli][light-strip]") {
    Sample sample("strip-add-refuse");
    sample.drawRoomSquare();
    const std::string before = readFile(sample.folder / "plan.json");

    const auto kind = sample.run({"light-strip", "add", "--kind", "neon", "--points", "1,3,4,3"});
    CHECK(kind.exitCode == kExitFailed);
    CHECK(errorCode(kind) == "invalid_value");
    const auto noKind = sample.run({"light-strip", "add", "--points", "1,3,4,3"});
    CHECK(noKind.exitCode == kExitBadArguments);
    CHECK(errorCode(noKind) == "bad_arguments");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,4"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,four,3"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,,3"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,1.05,3"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,4,3", "--z", "3.5"})) ==
          "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,4,3", "--power", "300"})) ==
          "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,4,3", "--stair", "s1"})) ==
          "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "step"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "add", "--kind", "step", "--stair", "s9"})) == "not_found");
    const auto floor = sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,3,4,3", "--floor", "f9"});
    CHECK(floor.exitCode == kExitFailed);
    CHECK(errorCode(floor) == "not_found");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("light-strip set changes the line, height, power and kind and keeps the rest", "[cli][light-strip]") {
    Sample sample("strip-set");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2"}).exitCode == kExitOk);

    const auto line = sample.run({"light-strip", "set", "ls1", "--points", "1.2,6.8,4.8,6.8", "--power", "15"});
    REQUIRE(line.exitCode == kExitOk);
    CHECK(line.answer.at("command") == "light-strip set");
    CHECK(line.answer.at("result").at("points").at(0).at("y") == 6.8);
    CHECK(line.answer.at("result").at("power") == 15.0);
    CHECK_THAT(line.answer.at("result").at("z").get<double>(), WithinAbs(2.95, 1e-9));

    const auto kind = sample.run({"light-strip", "set", "ls1", "--kind", "soffit"});
    REQUIRE(kind.exitCode == kExitOk);
    CHECK(kind.answer.at("result").at("kind") == "soffit");
    CHECK_THAT(kind.answer.at("result").at("z").get<double>(), WithinAbs(2.95, 1e-9));
    CHECK(kind.answer.at("result").at("points").size() == 2);

    const auto height = sample.run({"light-strip", "set", "ls1", "--z", "2.2"});
    REQUIRE(height.exitCode == kExitOk);
    CHECK(height.answer.at("result").at("z") == 2.2);
    CHECK(height.answer.at("result").at("power") == 15.0);
}

TEST_CASE("light-strip set to step drops the points and from step drops the stair", "[cli][light-strip]") {
    Sample sample("strip-set-step");
    sample.drawRoomSquare();
    addStair(sample);
    REQUIRE(sample.run({"light-strip", "add", "--kind", "edge", "--points", "1,7,5,7"}).exitCode == kExitOk);

    const auto step = sample.run({"light-strip", "set", "ls1", "--kind", "step", "--stair", "s1"});
    REQUIRE(step.exitCode == kExitOk);
    CHECK(step.answer.at("result").at("kind") == "step");
    CHECK(step.answer.at("result").at("points").empty());
    CHECK(step.answer.at("result").at("stair") == "s1");

    const auto edge = sample.run({"light-strip", "set", "ls1", "--kind", "edge", "--points", "1,7,5,7"});
    REQUIRE(edge.exitCode == kExitOk);
    CHECK(edge.answer.at("result").at("kind") == "edge");
    CHECK(edge.answer.at("result").at("stair").is_null());
    CHECK(edge.answer.at("result").at("points").size() == 2);
}

TEST_CASE("light-strip set refuses an unknown strip, a bad value and no option, changing nothing", "[cli][light-strip]") {
    Sample sample("strip-set-refuse");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2"}).exitCode == kExitOk);
    REQUIRE(sample.run({"light", "add", "--x", "3", "--y", "5"}).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");

    CHECK(errorCode(sample.run({"light-strip", "set", "ls9", "--z", "1"})) == "not_found");
    CHECK(errorCode(sample.run({"light-strip", "set", "l1", "--z", "1"})) == "not_found");
    CHECK(errorCode(sample.run({"light-strip", "set", "ls1", "--kind", "neon"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "set", "ls1", "--z", "4"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "set", "ls1", "--points", "1,3"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "set", "ls1", "--points", "1,3,4"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"light-strip", "set", "ls1", "--kind", "step"})) == "invalid_value");
    CHECK(errorCode(sample.run({"light-strip", "set", "ls1", "--stair", "s1"})) == "invalid_value");
    const auto none = sample.run({"light-strip", "set", "ls1"});
    CHECK(none.exitCode == kExitBadArguments);
    CHECK(errorCode(none) == "bad_arguments");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("light-strip remove and light-strip list", "[cli][light-strip]") {
    Sample sample("strip-remove");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2"}).exitCode == kExitOk);
    REQUIRE(sample.run({"light-strip", "add", "--kind", "edge", "--points", "1,7,5,7"}).exitCode == kExitOk);
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1,1,2,1", "--floor", "f2"}).exitCode ==
            kExitOk);

    const auto listed = sample.run({"light-strip", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    CHECK(listed.answer.at("result").at("floor") == "f1");
    const auto& strips = listed.answer.at("result").at("light_strips");
    REQUIRE(strips.size() == 2);
    CHECK(strips.at(0).at("id") == "ls1");
    CHECK(strips.at(1).at("kind") == "edge");
    const auto upstairs = sample.run({"light-strip", "list", "--floor", "f2"});
    REQUIRE(upstairs.answer.at("result").at("light_strips").size() == 1);
    CHECK(upstairs.answer.at("result").at("light_strips").at(0).at("id") == "ls3");

    const auto removed = sample.run({"light-strip", "remove", "ls1"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("command") == "light-strip remove");
    CHECK(removed.answer.at("result").at("removed") == "ls1");
    const auto after = sample.run({"light-strip", "list"});
    REQUIRE(after.answer.at("result").at("light_strips").size() == 1);
    CHECK(after.answer.at("result").at("light_strips").at(0).at("id") == "ls2");

    const std::string before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"light-strip", "remove", "ls1"})) == "not_found");
    CHECK(errorCode(sample.run({"light-strip", "remove", "r1"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(errorCode(sample.run({"light-strip", "list", "--floor", "f9"})) == "not_found");
}

TEST_CASE("stair remove also removes the step strips of the stair", "[cli][light-strip]") {
    Sample sample("strip-stair-remove");
    sample.drawRoomSquare();
    addStair(sample);
    REQUIRE(sample.run({"light-strip", "add", "--kind", "step", "--stair", "s1"}).exitCode == kExitOk);
    REQUIRE(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2"}).exitCode == kExitOk);

    REQUIRE(sample.run({"stair", "remove", "s1"}).exitCode == kExitOk);
    const auto listed = sample.run({"light-strip", "list"});
    const auto& strips = listed.answer.at("result").at("light_strips");
    REQUIRE(strips.size() == 1);
    CHECK(strips.at(0).at("id") == "ls2");
}

TEST_CASE("the remove commands of other kinds point a strip id to light-strip remove", "[cli][light-strip]") {
    Sample sample("strip-other-remove");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2"}).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"item", "remove", "ls1"}, {"opening", "remove", "ls1"}, {"wall", "remove", "ls1", "--cascade"}}) {
        INFO(args.front());
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "invalid_value");
        CHECK(refused.answer.at("error").at("message").get<std::string>().find("light-strip remove") !=
              std::string::npos);
    }
    CHECK(errorCode(sample.run({"light", "remove", "ls1"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("light-strip edits follow the lock rules and light-strip list never is refused", "[cli][light-strip]") {
    Sample sample("strip-lock");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2"}).exitCode == kExitOk);
    appOpens(sample.folder);
    const std::string before = readFile(sample.folder / "plan.json");

    for (const auto& args : std::vector<std::vector<std::string>>{
             {"light-strip", "add", "--kind", "edge", "--points", "1,7,5,7"},
             {"light-strip", "set", "ls1", "--z", "2"},
             {"light-strip", "remove", "ls1"}}) {
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(sample.run({"light-strip", "list"}).exitCode == kExitOk);

    REQUIRE(sample.run({"session", "begin"}).exitCode == kExitOk);
    CHECK(sample.run({"light-strip", "set", "ls1", "--z", "2"}).exitCode == kExitOk);
    REQUIRE(sample.run({"session", "end"}).exitCode == kExitOk);
}

TEST_CASE("help lists the light-strip commands", "[cli][light-strip]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"light-strip add", "light-strip set", "light-strip remove", "light-strip list"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
    CHECK(plynth::cli::isCommandWord("light-strip"));
}
