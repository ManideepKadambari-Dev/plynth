#include "SampleProject.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::TempFolder;

using plynth::cli::testing::errorCode;
using plynth::cli::testing::ids;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;

TEST_CASE("scale from-points measures the scale from two image points", "[cli][scale]") {
    Sample sample("fromp");
    const auto outcome = sample.run({"scale", "from-points", "--x1", "100", "--y1", "100", "--x2", "600", "--y2",
                                     "100", "--distance", "5"});
    CHECK(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("pixels_per_metre") == 100.0);
    CHECK(result.at("pixel_distance") == 500.0);
    CHECK(result.at("distance") == 5.0);
    CHECK(result.at("moved_marks") == false);
}

TEST_CASE("scale from-points refuses bad measurements and leaves the plan unchanged", "[cli][scale]") {
    Sample sample("fromp-bad");
    const auto before = readFile(sample.folder / "plan.json");
    const auto same = sample.run({"scale", "from-points", "--x1", "10", "--y1", "10", "--x2", "10.2", "--y2", "10",
                                  "--distance", "5"});
    CHECK(same.exitCode == kExitFailed);
    CHECK(errorCode(same) == "invalid_value");
    const auto zero = sample.run({"scale", "from-points", "--x1", "0", "--y1", "0", "--x2", "500", "--y2", "0",
                                  "--distance", "0"});
    CHECK(errorCode(zero) == "invalid_value");
    const auto missing = sample.run({"scale", "from-points", "--x1", "0", "--y1", "0"});
    CHECK(missing.exitCode == kExitBadArguments);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("changing the scale moves marks with --keep image and not with --keep metres", "[cli][scale]") {
    Sample sample("keep");
    REQUIRE(sample.run({"scale", "set", "--pixels-per-metre", "100"}).exitCode == kExitOk);
    REQUIRE(sample.run({"point", "add", "--x", "4", "--y", "2"}).exitCode == kExitOk);

    const auto image = sample.run({"scale", "set", "--pixels-per-metre", "200"});
    CHECK(image.answer.at("result").at("moved_marks") == true);
    CHECK(sample.run({"point", "list"}).answer.at("result").at("points").at(0).at("x") == 2.0);

    const auto metres = sample.run({"scale", "set", "--pixels-per-metre", "50", "--keep", "metres"});
    CHECK(metres.answer.at("result").at("moved_marks") == false);
    CHECK(sample.run({"point", "list"}).answer.at("result").at("points").at(0).at("x") == 2.0);

    const auto bad = sample.run({"scale", "set", "--pixels-per-metre", "50", "--keep", "both"});
    CHECK(bad.exitCode == kExitBadArguments);
}

TEST_CASE("the first scale moves nothing", "[cli][scale]") {
    Sample sample("first");
    REQUIRE(sample.run({"point", "add", "--x", "4", "--y", "2"}).exitCode == kExitOk);
    const auto outcome = sample.run({"scale", "set", "--pixels-per-metre", "80"});
    CHECK(outcome.answer.at("result").at("moved_marks") == false);
    CHECK(sample.run({"point", "list"}).answer.at("result").at("points").at(0).at("x") == 4.0);
}

TEST_CASE("project set changes the grid and refuses zero", "[cli][project]") {
    Sample sample("grid");
    const auto good = sample.run({"project", "set", "--grid", "0.25"});
    CHECK(good.exitCode == kExitOk);
    CHECK(good.answer.at("result").at("grid_size") == 0.25);

    const auto before = readFile(sample.folder / "project.json");
    const auto zero = sample.run({"project", "set", "--grid", "0"});
    CHECK(zero.exitCode == kExitFailed);
    CHECK(errorCode(zero) == "invalid_value");
    CHECK(readFile(sample.folder / "project.json") == before);
}

TEST_CASE("point move, list and remove", "[cli][point]") {
    Sample sample("point");
    REQUIRE(sample.run({"point", "add", "--x", "1", "--y", "1"}).exitCode == kExitOk);
    REQUIRE(sample.run({"point", "add", "--x", "3", "--y", "1"}).exitCode == kExitOk);

    const auto moved = sample.run({"point", "move", "p1", "--x", "2.5", "--y", "-1"});
    CHECK(moved.exitCode == kExitOk);
    CHECK(moved.answer.at("result").at("id") == "p1");
    CHECK(moved.answer.at("result").at("x") == 2.5);
    CHECK(moved.answer.at("result").at("y") == -1.0);
    CHECK(moved.answer.at("result").at("walls").empty());

    const auto listed = sample.run({"point", "list"});
    CHECK(listed.answer.at("result").at("floor") == "f1");
    CHECK(listed.answer.at("result").at("points").size() == 2);
    CHECK(listed.answer.at("result").at("points").at(0).at("x") == 2.5);

    const auto removed = sample.run({"point", "remove", "p2"});
    CHECK(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed").at("points") == nlohmann::json::array({"p2"}));
    CHECK(removed.answer.at("result").at("removed").at("walls").empty());
    CHECK(sample.run({"point", "list"}).answer.at("result").at("points").size() == 1);
}

TEST_CASE("point move on an unknown id gives not_found and leaves plan.json unchanged", "[cli][point]") {
    Sample sample("move-none");
    sample.run({"point", "add", "--x", "1", "--y", "1"});
    const auto before = readFile(sample.folder / "plan.json");
    const auto outcome = sample.run({"point", "move", "p9", "--x", "2", "--y", "2"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("point remove of a point in use gives in_use", "[cli][point]") {
    Sample sample("point-use");
    sample.drawSquare();
    const auto before = readFile(sample.folder / "plan.json");
    const auto outcome = sample.run({"point", "remove", "p1"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "in_use");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("point list with an unknown floor gives not_found", "[cli][point]") {
    Sample sample("point-floor");
    const auto outcome = sample.run({"point", "list", "--floor", "f9"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
}

TEST_CASE("wall draw chains walls and reports the wall entry", "[cli][wall]") {
    Sample sample("draw");
    const auto first = sample.draw({"--from-x", "1", "--from-y", "1", "--to-x", "7", "--to-y", "1", "--thickness",
                                    "0.3"});
    CHECK(first.exitCode == kExitOk);
    const auto& wall = first.answer.at("result");
    CHECK(wall.at("id") == "w1");
    CHECK(wall.at("floor") == "f1");
    CHECK(wall.at("from") == "p1");
    CHECK(wall.at("to") == "p2");
    CHECK(wall.at("thickness") == 0.3);
    CHECK(wall.at("height").is_null());
    CHECK(wall.at("bulge") == 0.0);
    CHECK_THAT(wall.at("length").get<double>(), WithinAbs(6.0, 1e-9));
    CHECK(wall.at("rooms").empty());
    CHECK(ids(wall.at("new_points")) == std::vector<std::string>{"p1", "p2"});
    CHECK(wall.at("split_walls").empty());

    const auto second = sample.draw({"--from", "p2", "--to-x", "7", "--to-y", "5"});
    CHECK(second.answer.at("result").at("id") == "w2");
    CHECK(second.answer.at("result").at("from") == "p2");
    CHECK(second.answer.at("result").at("to") == "p3");
    CHECK(ids(second.answer.at("result").at("new_points")) == std::vector<std::string>{"p3"});
}

TEST_CASE("wall draw across walls with --snap splits them", "[cli][wall]") {
    Sample sample("draw-split");
    sample.drawSquare();
    const auto outcome = sample.draw({"--from-x", "3", "--from-y", "1.05", "--to-x", "3", "--to-y", "4.95",
                                      "--snap", "0.1"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(ids(outcome.answer.at("result").at("split_walls")) == std::vector<std::string>{"w1", "w3"});
    CHECK(outcome.answer.at("result").at("new_points").size() == 2);

    const auto walls = sample.run({"wall", "list"});
    CHECK(walls.answer.at("result").at("walls").size() == 7);
    const auto points = sample.run({"point", "list"});
    CHECK(points.answer.at("result").at("points").size() == 6);
}

TEST_CASE("wall draw without --snap places new points exactly", "[cli][wall]") {
    Sample sample("draw-exact");
    sample.drawSquare();
    const auto outcome = sample.draw({"--from-x", "3", "--from-y", "1.05", "--to-x", "3", "--to-y", "3"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("split_walls").empty());
    CHECK(outcome.answer.at("result").at("new_points").size() == 2);
}

TEST_CASE("wall draw snaps new positions to the grid only with --grid", "[cli][wall]") {
    Sample sample("draw-grid");
    REQUIRE(sample.run({"project", "set", "--grid", "0.5"}).exitCode == kExitOk);
    const auto outcome = sample.draw({"--from-x", "1.1", "--from-y", "0.9", "--to-x", "3.2", "--to-y", "0.9",
                                      "--grid"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto points = sample.run({"point", "list"}).answer.at("result").at("points");
    CHECK(points.at(0).at("x") == 1.0);
    CHECK(points.at(0).at("y") == 1.0);
    CHECK(points.at(1).at("x") == 3.0);

    const auto plain = sample.draw({"--from", "p2", "--to-x", "3.2", "--to-y", "2.6"});
    REQUIRE(plain.exitCode == kExitOk);
    const auto after = sample.run({"point", "list"}).answer.at("result").at("points");
    CHECK(after.at(2).at("x") == 3.2);
    CHECK(after.at(2).at("y") == 2.6);
}

TEST_CASE("wall draw with both or neither form of an end gives bad_arguments", "[cli][wall]") {
    Sample sample("draw-args");
    sample.drawSquare();
    const auto before = readFile(sample.folder / "plan.json");
    const std::vector<std::vector<std::string>> cases = {
        {"--from", "p1", "--from-x", "1", "--from-y", "1", "--to-x", "9", "--to-y", "9"},
        {"--to-x", "9", "--to-y", "9"},
        {"--from", "p1", "--to-x", "9"},
        {"--from", "p1", "--to", "p2", "--to-y", "3"},
        {"--from", "p1"},
    };
    for (const auto& ends : cases) {
        const auto outcome = sample.draw(ends);
        CHECK(outcome.exitCode == kExitBadArguments);
        CHECK(errorCode(outcome) == "bad_arguments");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("wall draw refuses an unknown point or floor and a repeated wall", "[cli][wall]") {
    Sample sample("draw-bad");
    sample.drawSquare();
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.draw({"--from", "p9", "--to", "p1"})) == "not_found");
    CHECK(errorCode(sample.draw({"--from", "p1", "--to", "p2"})) == "already_exists");
    CHECK(errorCode(sample.draw({"--from", "p1", "--to-x", "9", "--to-y", "9", "--floor", "f9"})) == "not_found");
    CHECK(errorCode(sample.draw({"--from", "p1", "--to-x", "9", "--to-y", "9", "--thickness", "0"})) ==
          "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("wall split, set, list and remove", "[cli][wall]") {
    Sample sample("wall");
    sample.drawSquare();

    const auto split = sample.run({"wall", "split", "w1", "--offset", "1.5"});
    REQUIRE(split.exitCode == kExitOk);
    const auto& result = split.answer.at("result");
    CHECK(result.at("point").at("id") == "p5");
    CHECK(result.at("point").at("x") == 2.5);
    CHECK(result.at("point").at("y") == 1.0);
    CHECK(ids(result.at("point").at("walls")) == std::vector<std::string>{"w1", "w5"});
    CHECK(result.at("walls").size() == 2);
    CHECK(result.at("walls").at(0).at("id") == "w1");
    CHECK(result.at("walls").at(0).at("to") == "p5");
    CHECK(result.at("walls").at(1).at("id") == "w5");
    CHECK(result.at("walls").at(1).at("from") == "p5");
    CHECK(result.at("walls").at(1).at("to") == "p2");
    CHECK_THAT(result.at("walls").at(0).at("length").get<double>(), WithinAbs(1.5, 1e-9));

    const auto tooFar = sample.run({"wall", "split", "w1", "--offset", "9"});
    CHECK(errorCode(tooFar) == "invalid_value");
    CHECK(errorCode(sample.run({"wall", "split", "w99", "--offset", "1"})) == "not_found");

    const auto thick = sample.run({"wall", "set", "w2", "--thickness", "0.35"});
    CHECK(thick.exitCode == kExitOk);
    CHECK(thick.answer.at("result").at("id") == "w2");
    CHECK(thick.answer.at("result").at("thickness") == 0.35);
    CHECK(errorCode(sample.run({"wall", "set", "w2", "--thickness", "-1"})) == "invalid_value");
    CHECK(errorCode(sample.run({"wall", "set", "w99", "--thickness", "0.2"})) == "not_found");

    const auto listed = sample.run({"wall", "list"});
    CHECK(listed.answer.at("result").at("floor") == "f1");
    CHECK(listed.answer.at("result").at("walls").size() == 5);

    const auto removed = sample.run({"wall", "remove", "w5"});
    CHECK(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed").at("walls") == nlohmann::json::array({"w5"}));
    CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() == 4);
    CHECK(errorCode(sample.run({"wall", "remove", "w5"})) == "not_found");
}

TEST_CASE("the new commands are listed by help", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"scale set", "scale from-points", "project set", "point add", "point move",
                               "point remove", "point list", "wall add", "wall draw", "wall split", "wall set",
                               "wall remove", "wall list"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
}

TEST_CASE("edits are refused with locked while the app has the project open and lists are not", "[cli][session]") {
    Sample sample("locked");
    sample.drawSquare();

    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const auto before = readFile(sample.folder / "plan.json");

    const std::vector<std::vector<std::string>> edits = {
        {"scale", "from-points", "--x1", "0", "--y1", "0", "--x2", "500", "--y2", "0", "--distance", "5"},
        {"project", "set", "--grid", "1"},
        {"point", "move", "p1", "--x", "2", "--y", "2"},
        {"point", "remove", "p1"},
        {"wall", "draw", "--from", "p1", "--to-x", "9", "--to-y", "9"},
        {"wall", "split", "w1", "--offset", "1"},
        {"wall", "set", "w1", "--thickness", "0.4"},
        {"wall", "remove", "w1"},
    };
    for (const auto& args : edits) {
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);

    CHECK(sample.run({"point", "list"}).exitCode == kExitOk);
    CHECK(sample.run({"wall", "list"}).exitCode == kExitOk);
}

TEST_CASE("wall draw across a room splits it and the larger part keeps the id", "[cli][wall][room]") {
    Sample sample("draw-room-split");
    sample.drawRoomSquare();
    const auto before = sample.run({"room", "list"}).answer.at("result").at("rooms");
    REQUIRE(before.size() == 1);
    const std::string roomId = before.at(0).at("id");

    const auto outcome = sample.draw({"--from-x", "2", "--from-y", "3.02", "--to-x", "2", "--to-y", "6.97", "--snap",
                                      "0.1"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& made = outcome.answer.at("result").at("new_rooms");
    REQUIRE(made.size() == 1);
    CHECK(outcome.answer.at("result").at("rooms").size() == 2);

    const auto rooms = sample.run({"room", "list"}).answer.at("result").at("rooms");
    REQUIRE(rooms.size() == 2);
    CHECK(rooms.at(0).at("id") == roomId);
    CHECK(rooms.at(1).at("id") == made.at(0));
    CHECK(rooms.at(1).at("type") == "living");
    CHECK(rooms.at(0).at("area").get<double>() > rooms.at(1).at("area").get<double>());
}

TEST_CASE("wall draw takes the thickness of the wall it joins unless --thickness is given", "[cli][wall]") {
    Sample sample("draw-thickness");
    REQUIRE(sample.draw({"--from-x", "1", "--from-y", "1", "--to-x", "5", "--to-y", "1", "--thickness", "0.35"})
                .exitCode == kExitOk);
    const auto joined = sample.draw({"--from", "p2", "--to-x", "5", "--to-y", "4"});
    REQUIRE(joined.exitCode == kExitOk);
    CHECK(joined.answer.at("result").at("thickness") == 0.35);

    const auto ending = sample.draw({"--from-x", "8", "--from-y", "4", "--to", "p3"});
    REQUIRE(ending.exitCode == kExitOk);
    CHECK(ending.answer.at("result").at("thickness") == 0.35);

    const auto given = sample.draw({"--from", "p1", "--to-x", "1", "--to-y", "4", "--thickness", "0.1"});
    REQUIRE(given.exitCode == kExitOk);
    CHECK(given.answer.at("result").at("thickness") == 0.1);

    const auto loose = sample.draw({"--from-x", "10", "--from-y", "1", "--to-x", "12", "--to-y", "1"});
    REQUIRE(loose.exitCode == kExitOk);
    CHECK(loose.answer.at("result").at("thickness") == 0.2);
}

TEST_CASE("wall draw with --snap lines the end up with the start", "[cli][wall]") {
    Sample sample("draw-align");
    const auto outcome = sample.draw({"--from-x", "1", "--from-y", "1", "--to-x", "5", "--to-y", "1.06", "--snap",
                                      "0.1"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto points = sample.run({"point", "list"}).answer.at("result").at("points");
    CHECK(points.at(1).at("x") == 5.0);
    CHECK(points.at(1).at("y") == 1.0);

    const auto exact = sample.draw({"--from-x", "1", "--from-y", "3", "--to-x", "5", "--to-y", "3.06"});
    REQUIRE(exact.exitCode == kExitOk);
    CHECK(sample.run({"point", "list"}).answer.at("result").at("points").at(3).at("y") == 3.06);
}

TEST_CASE("point join moves the walls onto the other point and matches the thickness", "[cli][point]") {
    Sample sample("join");
    sample.drawSquare();
    REQUIRE(sample.draw({"--from-x", "7", "--from-y", "7", "--to-x", "5.1", "--to-y", "5.1", "--thickness", "0.1"})
                .exitCode == kExitOk);
    const auto outcome = sample.run({"point", "join", "p6", "--to", "p3"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("id") == "p3");
    CHECK(result.at("removed_point") == "p6");
    CHECK(result.at("new_rooms").empty());
    CHECK(result.at("walls").size() == 3);
    const auto walls = sample.run({"wall", "list"}).answer.at("result").at("walls");
    CHECK(walls.at(4).at("to") == "p3");
    CHECK(walls.at(4).at("thickness") == 0.2);
    CHECK(sample.run({"point", "list"}).answer.at("result").at("points").size() == 5);
}

TEST_CASE("point join onto a wall splits it there", "[cli][point]") {
    Sample sample("join-wall");
    sample.drawSquare();
    REQUIRE(sample.draw({"--from-x", "3", "--from-y", "-2", "--to-x", "3", "--to-y", "0.9"}).exitCode == kExitOk);
    const auto outcome = sample.run({"point", "join", "p6", "--wall", "w1", "--offset", "2"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("x") == 3.0);
    CHECK(outcome.answer.at("result").at("y") == 1.0);
    CHECK(outcome.answer.at("result").at("walls").size() == 3);
    CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() == 6);
}

TEST_CASE("point join refuses bad targets and leaves plan.json unchanged", "[cli][point]") {
    Sample sample("join-bad");
    sample.drawSquare();
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(sample.run({"point", "join", "p1"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"point", "join", "p1", "--to", "p3", "--wall", "w2", "--offset", "1"}).exitCode ==
          kExitBadArguments);
    CHECK(sample.run({"point", "join", "p1", "--wall", "w2"}).exitCode == kExitBadArguments);
    CHECK(errorCode(sample.run({"point", "join", "p1", "--to", "p2"})) == "invalid_value");
    CHECK(errorCode(sample.run({"point", "join", "p1", "--to", "p1"})) == "invalid_value");
    CHECK(errorCode(sample.run({"point", "join", "p1", "--to", "p3"})) == "already_exists");
    CHECK(errorCode(sample.run({"point", "join", "p9", "--to", "p3"})) == "not_found");
    CHECK(errorCode(sample.run({"point", "join", "p1", "--wall", "w1", "--offset", "1"})) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
}
