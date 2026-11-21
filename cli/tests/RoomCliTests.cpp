#include "SampleProject.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::ids;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

namespace {

// The three room project: a 6 by 4 m building with 0.3 m walls split by a wall at x 4, a 1 by 1 m island
// of 0.2 m walls in the right half, and a loose wall running into the left half. Returns the id of the loose wall.
std::string buildProject(const Sample& sample) {
    REQUIRE(sample.run({"scale", "from-points", "--x1", "100", "--y1", "100", "--x2", "600", "--y2", "100",
                        "--distance", "5"})
                .exitCode == kExitOk);
    const std::vector<std::vector<std::string>> steps = {
        {"--from-x", "1", "--from-y", "1", "--to-x", "7", "--to-y", "1", "--thickness", "0.3"},
        {"--from", "p2", "--to-x", "7", "--to-y", "5", "--thickness", "0.3"},
        {"--from", "p3", "--to-x", "1", "--to-y", "5", "--thickness", "0.3"},
        {"--from", "p4", "--to", "p1", "--thickness", "0.3"},
        {"--from-x", "4", "--from-y", "1", "--to-x", "4", "--to-y", "5", "--snap", "0.1", "--thickness", "0.3"},
        {"--from-x", "5", "--from-y", "2.5", "--to-x", "6", "--to-y", "2.5"},
    };
    for (const auto& step : steps) {
        REQUIRE(sample.draw(step).exitCode == kExitOk);
    }
    std::string last = sample.run({"wall", "list"}).answer.at("result").at("walls").back().at("to");
    for (const std::vector<std::string>& end : {std::vector<std::string>{"--to-x", "6", "--to-y", "3.5"},
                                                 std::vector<std::string>{"--to-x", "5", "--to-y", "3.5"}}) {
        std::vector<std::string> args = {"--from", last};
        args.insert(args.end(), end.begin(), end.end());
        const auto outcome = sample.draw(args);
        REQUIRE(outcome.exitCode == kExitOk);
        last = outcome.answer.at("result").at("to");
    }
    const auto first = sample.run({"wall", "list"}).answer.at("result").at("walls");
    std::string islandStart;
    for (const auto& wall : first) {
        if (wall.at("thickness") == 0.2) {
            islandStart = wall.at("from");
            break;
        }
    }
    REQUIRE(sample.draw({"--from", last, "--to", islandStart}).exitCode == kExitOk);
    const auto loose = sample.draw({"--from-x", "2", "--from-y", "5", "--snap", "0.1", "--to-x", "2", "--to-y",
                                    "4"});
    REQUIRE(loose.exitCode == kExitOk);
    return loose.answer.at("result").at("id");
}

// Adds the three rooms of the project.
void addRooms(const Sample& sample) {
    REQUIRE(sample.run({"room", "add", "--x", "2.5", "--y", "3", "--type", "living"}).exitCode == kExitOk);
    REQUIRE(sample.run({"room", "add", "--x", "5.5", "--y", "1.5", "--type", "bedroom"}).exitCode == kExitOk);
    REQUIRE(sample.run({"room", "add", "--x", "5.5", "--y", "3", "--type", "storage"}).exitCode == kExitOk);
}

}

TEST_CASE("the check script makes three rooms with the right areas", "[cli][room]") {
    Sample sample("script");
    const std::string loose = buildProject(sample);
    addRooms(sample);

    const auto listed = sample.run({"room", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    const auto& rooms = listed.answer.at("result").at("rooms");
    REQUIRE(rooms.size() == 3);
    CHECK(rooms.at(0).at("id") == "r1");
    CHECK(rooms.at(0).at("type") == "living");
    CHECK(rooms.at(0).at("type_name") == "Living room");
    CHECK(rooms.at(0).at("closed") == true);
    CHECK_THAT(rooms.at(0).at("area").get<double>(), WithinAbs(9.99, 1e-4));
    CHECK(rooms.at(1).at("type_name") == "Bedroom");
    CHECK_THAT(rooms.at(1).at("area").get<double>(), WithinAbs(8.55, 1e-4));
    CHECK(rooms.at(2).at("type_name") == "Storage");
    CHECK_THAT(rooms.at(2).at("area").get<double>(), WithinAbs(0.64, 1e-4));
    CHECK(rooms.at(0).at("label").at("x").is_number());

    for (const auto& room : rooms) {
        const auto walls = ids(room.at("walls"));
        CHECK(std::ranges::find(walls, loose) == walls.end());
    }
    const auto walls = sample.run({"wall", "list"}).answer.at("result").at("walls");
    for (const auto& wall : walls) {
        if (wall.at("id") == loose) {
            CHECK(wall.at("rooms").empty());
        }
    }
}

TEST_CASE("room find shows no room before room add and the id after", "[cli][room]") {
    Sample sample("find");
    buildProject(sample);

    const auto before = sample.run({"room", "find"});
    REQUIRE(before.exitCode == kExitOk);
    const auto& areas = before.answer.at("result").at("areas");
    REQUIRE(areas.size() == 3);
    std::vector<double> sizes;
    for (const auto& area : areas) {
        CHECK(area.at("room").is_null());
        CHECK(area.at("inside").at("x").is_number());
        CHECK_FALSE(area.at("walls").empty());
        sizes.push_back(area.at("centre_line_area").get<double>());
    }
    std::ranges::sort(sizes);
    CHECK_THAT(sizes.at(0), WithinAbs(1.0, 1e-4));

    addRooms(sample);
    const auto after = sample.run({"room", "find"});
    for (const auto& area : after.answer.at("result").at("areas")) {
        CHECK(area.at("room").is_string());
    }
}

TEST_CASE("room add refuses a position outside, inside a room and an unknown type", "[cli][room]") {
    Sample sample("room-bad");
    buildProject(sample);
    addRooms(sample);
    const auto before = readFile(sample.folder / "plan.json");

    const auto outside = sample.run({"room", "add", "--x", "20", "--y", "20"});
    CHECK(outside.exitCode == kExitFailed);
    CHECK(errorCode(outside) == "not_found");
    const auto twice = sample.run({"room", "add", "--x", "2.5", "--y", "3"});
    CHECK(errorCode(twice) == "already_exists");
    const auto type = sample.run({"room", "add", "--x", "2.5", "--y", "3", "--type", "dungeon"});
    CHECK(errorCode(type) == "invalid_value");
    CHECK(type.answer.at("error").at("message").get<std::string>().find("living") != std::string::npos);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("room set changes and clears the type", "[cli][room]") {
    Sample sample("room-set");
    buildProject(sample);
    addRooms(sample);

    const auto kitchen = sample.run({"room", "set", "r1", "--type", "kitchen"});
    CHECK(kitchen.exitCode == kExitOk);
    CHECK(kitchen.answer.at("result").at("type") == "kitchen");
    CHECK(kitchen.answer.at("result").at("type_name") == "Kitchen");

    const auto none = sample.run({"room", "set", "r1", "--type", "none"});
    CHECK(none.exitCode == kExitOk);
    CHECK(none.answer.at("result").at("type").is_null());
    CHECK(none.answer.at("result").at("type_name") == "Room");

    CHECK(errorCode(sample.run({"room", "set", "r1", "--type", "dungeon"})) == "invalid_value");
    CHECK(errorCode(sample.run({"room", "set", "r99", "--type", "hall"})) == "not_found");
}

TEST_CASE("room remove keeps the walls", "[cli][room]") {
    Sample sample("room-remove");
    buildProject(sample);
    addRooms(sample);
    const auto walls = sample.run({"wall", "list"}).answer.at("result").at("walls").size();

    const auto removed = sample.run({"room", "remove", "r2"});
    CHECK(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed").at("rooms") == nlohmann::json::array({"r2"}));
    CHECK(sample.run({"room", "list"}).answer.at("result").at("rooms").size() == 2);
    CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() == walls);
    CHECK(errorCode(sample.run({"room", "remove", "r2"})) == "not_found");
}

TEST_CASE("wall remove of a room wall gives in_use and --cascade removes the room too", "[cli][room]") {
    Sample sample("cascade");
    buildProject(sample);
    addRooms(sample);
    const auto before = readFile(sample.folder / "plan.json");
    const auto rooms = sample.run({"room", "list"}).answer.at("result").at("rooms");
    const std::string wall = rooms.at(0).at("walls").at(0);

    const auto strict = sample.run({"wall", "remove", wall});
    CHECK(strict.exitCode == kExitFailed);
    CHECK(errorCode(strict) == "in_use");
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto cascade = sample.run({"wall", "remove", wall, "--cascade"});
    REQUIRE(cascade.exitCode == kExitOk);
    const auto& removed = cascade.answer.at("result").at("removed");
    CHECK(ids(removed.at("walls")).front() == wall);
    CHECK(ids(removed.at("rooms")).front() == "r1");
    const auto left = sample.run({"room", "list"}).answer.at("result").at("rooms");
    CHECK(left.size() == 2);
}

TEST_CASE("point remove --cascade removes its walls, rooms and loose end points", "[cli][room]") {
    Sample sample("cascade-point");
    buildProject(sample);
    addRooms(sample);

    const auto cascade = sample.run({"point", "remove", "p1", "--cascade"});
    REQUIRE(cascade.exitCode == kExitOk);
    const auto& removed = cascade.answer.at("result").at("removed");
    CHECK(ids(removed.at("points")).front() == "p1");
    CHECK_FALSE(removed.at("walls").empty());
    CHECK(removed.at("rooms").size() == 1);

    CHECK(errorCode(sample.run({"point", "remove", "p99", "--cascade"})) == "not_found");
}

TEST_CASE("point remove and wall remove with --cascade work on a plain wall", "[cli][room]") {
    Sample sample("cascade-plain");
    sample.drawSquare();
    const auto outcome = sample.run({"wall", "remove", "w1", "--cascade"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("removed").at("walls") == nlohmann::json::array({"w1"}));
    CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() == 3);
}

TEST_CASE("remove --cascade refuses an id of the wrong kind", "[cli][room]") {
    Sample sample("cascade-kind");
    sample.drawSquare();
    const auto before = readFile(sample.folder / "plan.json");

    const auto wallAsPoint = sample.run({"point", "remove", "w1", "--cascade"});
    CHECK(wallAsPoint.exitCode == kExitFailed);
    CHECK(errorCode(wallAsPoint) == "invalid_value");
    CHECK(wallAsPoint.answer.at("error").at("message") == "w1 is a wall, not a point. Use wall remove.");
    const auto pointAsWall = sample.run({"wall", "remove", "p1", "--cascade"});
    CHECK(errorCode(pointAsWall) == "invalid_value");
    CHECK(pointAsWall.answer.at("error").at("message") == "p1 is a point, not a wall. Use point remove.");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("room types needs no project", "[cli][room]") {
    plynth::cli::testing::TempFolder temp("types");
    const auto outcome = runChecked({"room", "types"}, temp.path());
    CHECK(outcome.exitCode == kExitOk);
    const auto& types = outcome.answer.at("result").at("room_types");
    REQUIRE(types.size() == 9);
    CHECK(types.at(0).at("id") == "living");
    CHECK(types.at(0).at("name") == "Living room");
    CHECK(types.at(8).at("id") == "other");
}

TEST_CASE("room edits are refused with locked while the app has the project open and lists are not", "[cli][room]") {
    Sample sample("room-locked");
    buildProject(sample);
    addRooms(sample);

    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const auto before = readFile(sample.folder / "plan.json");

    const std::vector<std::vector<std::string>> edits = {
        {"room", "add", "--x", "1", "--y", "1"},
        {"room", "set", "r1", "--type", "hall"},
        {"room", "remove", "r1"},
        {"wall", "remove", "w1", "--cascade"},
        {"point", "remove", "p1", "--cascade"},
    };
    for (const auto& args : edits) {
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);

    CHECK(sample.run({"room", "list"}).exitCode == kExitOk);
    CHECK(sample.run({"room", "find"}).exitCode == kExitOk);
    CHECK(runChecked({"room", "types"}).exitCode == kExitOk);
}

TEST_CASE("the room commands are listed by help", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"room add", "room set", "room remove", "room list", "room find", "room types"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
}
