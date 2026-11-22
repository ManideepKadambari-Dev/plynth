#include "MaterialLibrary.h"
#include "SampleProject.h"

#include <plynth/cli/Cli.h>

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
using plynth::cli::testing::MaterialLibrary;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

TEST_CASE("stair add on a floor with a floor above gives the worked out values", "[cli][stair]") {
    Sample sample("stair-add");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    const auto added = sample.run({"stair", "add", "--floor", "f1", "--x", "1", "--y", "1"});
    REQUIRE(added.exitCode == kExitOk);
    const auto& entry = added.answer.at("result");
    CHECK(entry.at("id") == "s1");
    CHECK(entry.at("floor") == "f1");
    CHECK(entry.at("to_floor") == "f2");
    CHECK(entry.at("type") == "straight");
    CHECK(entry.at("riser_count") == 17);
    CHECK_THAT(entry.at("rise").get<double>(), WithinAbs(3.2, 1e-9));
    CHECK_THAT(entry.at("riser").get<double>(), WithinAbs(3.2 / 17.0, 1e-5));
    CHECK_THAT(entry.at("length").get<double>(), WithinAbs(4.32, 1e-3));
    CHECK(entry.at("footprint").size() >= 4);
    CHECK(entry.at("arrival").size() == 2);
    CHECK(entry.at("material").is_null());

    const auto list = sample.run({"stair", "list"});
    REQUIRE(list.exitCode == kExitOk);
    CHECK(list.answer.at("result").at("stairs").size() == 1);
    CHECK(sample.run({"stair", "list", "--floor", "f2"}).answer.at("result").at("stairs").empty());
    CHECK(errorCode(sample.run({"stair", "list", "--floor", "f9"})) == "not_found");
    CHECK(sample.run({"floor", "list"}).answer.at("result").at("floors").at(0).at("stairs").at(0) == "s1");
}

TEST_CASE("stair add on the top floor is refused", "[cli][stair]") {
    Sample sample("stair-top");
    const auto before = readFile(sample.folder / "plan.json");
    const auto refused = sample.run({"stair", "add", "--x", "1", "--y", "1"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("stair add refuses bad values and missing positions", "[cli][stair]") {
    Sample sample("stair-bad");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"stair", "add", "--x", "1", "--y", "1", "--width", "0.3"})) == "invalid_value");
    CHECK(errorCode(sample.run({"stair", "add", "--x", "1", "--y", "1", "--type", "ladder"})) == "invalid_value");
    CHECK(errorCode(sample.run({"stair", "add", "--floor", "f9", "--x", "1", "--y", "1"})) == "not_found");
    CHECK(sample.run({"stair", "add", "--x", "1"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"stair", "add", "--x", "1", "--y", "1", "--turn", "up"}).exitCode == kExitBadArguments);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("stair set changes the type and turn and refuses an empty change", "[cli][stair]") {
    Sample sample("stair-set");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"stair", "add", "--x", "1", "--y", "1"}).exitCode == kExitOk);
    const auto set = sample.run({"stair", "set", "s1", "--type", "spiral", "--turn", "left", "--x", "2"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("type") == "spiral");
    CHECK(set.answer.at("result").at("turn") == "left");
    CHECK(set.answer.at("result").at("x") == 2.0);
    CHECK(set.answer.at("result").at("y") == 1.0);

    const auto before = readFile(sample.folder / "plan.json");
    CHECK(sample.run({"stair", "set", "s1"}).exitCode == kExitBadArguments);
    CHECK(errorCode(sample.run({"stair", "set", "s9", "--x", "1"})) == "not_found");
    CHECK(errorCode(sample.run({"stair", "set", "s1", "--width", "9"})) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("stair remove removes and stair types lists the four types", "[cli][stair]") {
    Sample sample("stair-remove");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"stair", "add", "--x", "1", "--y", "1"}).exitCode == kExitOk);
    CHECK(errorCode(sample.run({"stair", "remove", "s9"})) == "not_found");
    const auto removed = sample.run({"stair", "remove", "s1"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == "s1");
    CHECK(sample.run({"stair", "list"}).answer.at("result").at("stairs").empty());

    const auto types = runChecked({"stair", "types"});
    REQUIRE(types.exitCode == kExitOk);
    std::vector<std::string> names;
    for (const auto& type : types.answer.at("result").at("types")) {
        names.push_back(type.at("id").get<std::string>());
        CHECK(type.at("uses_turn").is_boolean());
        CHECK(type.at("name").is_string());
    }
    CHECK(names == std::vector<std::string>{"straight", "l_shape", "u_shape", "spiral"});
}

TEST_CASE("item, wall, point, room and opening remove refuse a stair id and name stair remove", "[cli][stair]") {
    Sample sample("stair-wrong-remove");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"stair", "add", "--x", "1", "--y", "1"}).exitCode == kExitOk);
    const auto before = readFile(sample.folder / "plan.json");
    for (const char* group : {"item", "wall", "point", "room", "opening"}) {
        const auto refused = sample.run({group, "remove", "s1"});
        INFO(group);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "invalid_value");
        const std::string message = refused.answer.at("error").at("message").get<std::string>();
        CHECK(message.find("a stair") != std::string::npos);
        CHECK(message.find("stair remove") != std::string::npos);
        CHECK(readFile(sample.folder / "plan.json") == before);
    }
    CHECK(sample.run({"stair", "list"}).answer.at("result").at("stairs").size() == 1);
}

TEST_CASE("a stair takes a material through stair add, stair set and material set", "[cli][stair]") {
    MaterialLibrary library("stair-material");
    Sample sample("stair-material-project");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"stair", "add", "--x", "1", "--y", "1"}).exitCode == kExitOk);

    const auto set = sample.run({"material", "set", "s1", "grey_tile"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("material") == "grey_tile");
    CHECK(set.answer.at("result").at("resolved").at("origin") == "stair");
    CHECK(sample.run({"stair", "list"}).answer.at("result").at("stairs").at(0).at("material") == "grey_tile");

    const auto cleared = sample.run({"stair", "set", "s1", "--material", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("material").is_null());
    CHECK(sample.run({"stair", "set", "s1", "--material", "red_brick"}).answer.at("result").at("material") ==
          "red_brick");
    CHECK(errorCode(sample.run({"stair", "set", "s1", "--material", "nothing_here"})) == "not_found");
    CHECK(errorCode(sample.run({"material", "set", "s9", "grey_tile"})) == "not_found");
    const auto added = sample.run({"stair", "add", "--x", "5", "--y", "1", "--material", "oak_planks"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("material") == "oak_planks");
}

TEST_CASE("help lists the floor and stair commands", "[cli][stair]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* name : {"floor add", "floor remove", "floor list", "floor set", "stair add", "stair set",
                             "stair remove", "stair list", "stair types"}) {
        CHECK(std::ranges::find(names, name) != names.end());
    }
}
