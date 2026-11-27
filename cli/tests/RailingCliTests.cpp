#include "SampleProject.h"

#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitBadArguments;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

namespace {

// Puts a deck with this id and outline on the project's first floor.
void addDeck(const Sample& sample, const std::string& id, const std::vector<plynth::PlanXY>& outline) {
    plynth::ProjectLoad load = plynth::loadProject(sample.folder);
    REQUIRE(load.status.ok());
    plynth::Deck deck;
    deck.id = id;
    deck.outline = outline;
    load.project.plan.floors[0].decks.push_back(deck);
    REQUIRE(plynth::saveProject(load.project, sample.folder, sample.folder).ok());
}

// Checks that a JSON line of [x, y] pairs matches the expected points.
void checkPoints(const nlohmann::json& points, const std::vector<plynth::PlanXY>& expected) {
    REQUIRE(points.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK_THAT(points.at(i).at(0).get<double>(), WithinAbs(expected[i].x, 1e-6));
        CHECK_THAT(points.at(i).at(1).get<double>(), WithinAbs(expected[i].y, 1e-6));
    }
}

}

TEST_CASE("railing add along points gives the railing entry and railing list shows it", "[cli][railing]") {
    Sample sample("railing-add");
    const auto added = sample.run({"railing", "add", "--points", "1,1,5,1,5,4", "--style", "spindles"});
    REQUIRE(added.exitCode == kExitOk);
    const auto& railings = added.answer.at("result").at("railings");
    REQUIRE(railings.size() == 1);
    const auto& entry = railings.at(0);
    CHECK(entry.at("id") == "rl1");
    CHECK(entry.at("floor") == "f1");
    checkPoints(entry.at("points"), {{1, 1}, {5, 1}, {5, 4}});
    CHECK(entry.at("closed") == false);
    CHECK(entry.at("host").is_null());
    CHECK(entry.at("base") == 0.0);
    CHECK(entry.at("height") == 1.0);
    CHECK(entry.at("style") == "spindles");
    CHECK(entry.at("material").is_null());
    CHECK(entry.at("glass_material").is_null());
    CHECK_THAT(entry.at("length").get<double>(), WithinAbs(7.0, 1e-6));
    CHECK(entry.at("runs") == 2);
    CHECK(entry.at("problems").empty());

    const auto spaced = sample.run({"railing", "add", "--points", "1,6 4,6 4,8", "--style", "glass", "--closed",
                                    "true", "--height", "1.1", "--base", "0.5"});
    REQUIRE(spaced.exitCode == kExitOk);
    const auto& closed = spaced.answer.at("result").at("railings").at(0);
    CHECK(closed.at("id") == "rl2");
    CHECK(closed.at("closed") == true);
    CHECK(closed.at("runs") == 3);
    CHECK(closed.at("base") == 0.5);

    const auto list = sample.run({"railing", "list"});
    REQUIRE(list.exitCode == kExitOk);
    CHECK(list.answer.at("result").at("railings").size() == 2);
    CHECK(sample.run({"railing", "list", "--floor", "f1"}).answer.at("result").at("railings").size() == 2);
    CHECK(errorCode(sample.run({"railing", "list", "--floor", "f9"})) == "not_found");
}

TEST_CASE("railing add refuses bad lines, styles and heights and changes nothing", "[cli][railing]") {
    Sample sample("railing-bad");
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(sample.run({"railing", "add", "--style", "glass"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"railing", "add", "--points", "1,1,5,1"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"railing", "add", "--points", "1,1,5", "--style", "glass"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"railing", "add", "--points", "1,1,x,1", "--style", "glass"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"railing", "add", "--points", "1,1,5,1", "--around", "dk1", "--style", "glass"}).exitCode ==
          kExitBadArguments);
    CHECK(sample.run({"railing", "add", "--points", "1,1,5,1", "--style", "glass", "--closed", "maybe"}).exitCode ==
          kExitBadArguments);
    CHECK(errorCode(sample.run({"railing", "add", "--points", "1,1,5,1", "--style", "rope"})) == "invalid_value");
    CHECK(errorCode(sample.run({"railing", "add", "--points", "1,1,1.05,1", "--style", "glass"})) == "invalid_value");
    CHECK(errorCode(sample.run({"railing", "add", "--points", "1,1,5,1", "--style", "glass", "--height", "2"})) ==
          "invalid_value");
    CHECK(errorCode(sample.run({"railing", "add", "--points", "1,1,5,1", "--style", "cables", "--height", "0.1"})) ==
          "invalid_value");
    CHECK(errorCode(sample.run({"railing", "add", "--points", "1,1,5,1", "--style", "cables", "--closed", "true"})) ==
          "invalid_value");
    CHECK(errorCode(sample.run({"railing", "add", "--floor", "f9", "--points", "1,1,5,1", "--style", "glass"})) ==
          "not_found");
    CHECK(errorCode(sample.run({"railing", "add", "--points", "1,1,5,1", "--style", "glass", "--host", "dk9"})) ==
          "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("railing add --around follows the open edges of a deck beside the walls", "[cli][railing][deck]") {
    Sample sample("railing-around");
    sample.drawSquare();
    addDeck(sample, "dk1", {{5.1, 1.0}, {8.0, 1.0}, {8.0, 3.0}, {5.1, 3.0}});
    const auto added = sample.run({"railing", "add", "--around", "dk1", "--style", "glass", "--height", "1.1"});
    REQUIRE(added.exitCode == kExitOk);
    const auto& railings = added.answer.at("result").at("railings");
    REQUIRE(railings.size() == 1);
    const auto& entry = railings.at(0);
    CHECK(entry.at("floor") == "f1");
    CHECK(entry.at("host") == "dk1");
    CHECK(entry.at("style") == "glass");
    CHECK(entry.at("closed") == false);
    checkPoints(entry.at("points"), {{5.1, 1.0}, {8.0, 1.0}, {8.0, 3.0}, {5.1, 3.0}});
    CHECK(entry.at("runs") == 3);
    CHECK_THAT(entry.at("length").get<double>(), WithinAbs(2.9 + 2.0 + 2.9, 1e-6));
    CHECK(entry.at("problems").empty());

    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"railing", "add", "--around", "dk9", "--style", "glass"})) == "not_found");
    CHECK(errorCode(sample.run({"railing", "add", "--floor", "f9", "--around", "dk1", "--style", "glass"})) ==
          "not_found");
    CHECK(sample.run({"railing", "add", "--around", "dk1", "--style", "glass", "--host", "dk1"}).exitCode ==
          kExitBadArguments);
    CHECK(sample.run({"railing", "add", "--around", "dk1", "--style", "glass", "--closed", "true"}).exitCode ==
          kExitBadArguments);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("railing add --around a free deck runs all round it and a deck inside a wall is refused",
          "[cli][railing][deck]") {
    Sample sample("railing-around-free");
    sample.drawSquare();
    addDeck(sample, "dk1", {{10.0, 1.0}, {12.0, 1.0}, {12.0, 3.0}, {10.0, 3.0}});
    addDeck(sample, "dk2", {{4.95, 2.0}, {5.05, 2.0}, {5.05, 4.0}, {4.95, 4.0}});
    const auto added = sample.run({"railing", "add", "--around", "dk1", "--style", "posts_rails"});
    REQUIRE(added.exitCode == kExitOk);
    const auto& entry = added.answer.at("result").at("railings").at(0);
    CHECK(entry.at("points").size() == 5);
    CHECK(entry.at("points").front() == entry.at("points").back());
    CHECK(entry.at("runs") == 4);
    CHECK_THAT(entry.at("length").get<double>(), WithinAbs(8.0, 1e-6));

    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"railing", "add", "--around", "dk2", "--style", "glass"})) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("railing set changes what is given and refuses an empty change", "[cli][railing]") {
    Sample sample("railing-set");
    addDeck(sample, "dk1", {{1.0, 1.0}, {4.0, 1.0}, {4.0, 3.0}, {1.0, 3.0}});
    REQUIRE(sample.run({"railing", "add", "--points", "1,1,4,1", "--style", "posts_rails"}).exitCode == kExitOk);
    const auto set = sample.run({"railing", "set", "rl1", "--style", "flat_bars", "--height", "1.2", "--host", "dk1"});
    REQUIRE(set.exitCode == kExitOk);
    const auto& entry = set.answer.at("result");
    CHECK(entry.at("style") == "flat_bars");
    CHECK(entry.at("height") == 1.2);
    CHECK(entry.at("host") == "dk1");
    checkPoints(entry.at("points"), {{1, 1}, {4, 1}});

    const auto moved = sample.run({"railing", "set", "rl1", "--points", "1,3,4,3,4,1", "--host", "none"});
    REQUIRE(moved.exitCode == kExitOk);
    CHECK(moved.answer.at("result").at("host").is_null());
    CHECK(moved.answer.at("result").at("runs") == 2);
    CHECK(moved.answer.at("result").at("style") == "flat_bars");

    const auto before = readFile(sample.folder / "plan.json");
    CHECK(sample.run({"railing", "set", "rl1"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"railing", "set", "rl1", "--points", "1"}).exitCode == kExitBadArguments);
    CHECK(errorCode(sample.run({"railing", "set", "rl9", "--height", "1"})) == "not_found");
    CHECK(errorCode(sample.run({"railing", "set", "rl1", "--host", "dk9"})) == "not_found");
    CHECK(errorCode(sample.run({"railing", "set", "rl1", "--style", "half_wall", "--height", "2"})) == "invalid_value");
    CHECK(errorCode(sample.run({"railing", "set", "rl1", "--base", "30"})) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("railing remove removes and railing styles lists the six styles with their heights", "[cli][railing]") {
    Sample sample("railing-remove");
    REQUIRE(sample.run({"railing", "add", "--points", "1,1,4,1", "--style", "cables"}).exitCode == kExitOk);
    CHECK(errorCode(sample.run({"railing", "remove", "rl9"})) == "not_found");
    const auto removed = sample.run({"railing", "remove", "rl1"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == "rl1");
    CHECK(sample.run({"railing", "list"}).answer.at("result").at("railings").empty());

    const auto styles = runChecked({"railing", "styles"});
    REQUIRE(styles.exitCode == kExitOk);
    std::vector<std::string> names;
    for (const auto& style : styles.answer.at("result").at("styles")) {
        names.push_back(style.at("id").get<std::string>());
        CHECK(style.at("name").is_string());
        CHECK(style.at("min_height").get<double>() < style.at("max_height").get<double>());
        if (style.at("id") == "glass" || style.at("id") == "half_wall") {
            CHECK(style.at("max_height") == 1.5);
        }
    }
    CHECK(names == std::vector<std::string>{"posts_rails", "spindles", "flat_bars", "glass", "cables", "half_wall"});
}
