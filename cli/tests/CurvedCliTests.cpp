#include "SampleProject.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>

using namespace plynth::cli;
using namespace plynth::cli::testing;
using Catch::Matchers::WithinAbs;

namespace {

// Draws the room square and checks its area.
void drawRoomSquare(const Sample& sample) {
    sample.drawRoomSquare();
    const auto rooms = sample.run({"room", "list"});
    CHECK_THAT(rooms.answer.at("result").at("rooms").at(0).at("area").get<double>(), WithinAbs(14.44, 1e-3));
}

}

TEST_CASE("wall set bends a wall into a half circle and reports length and radius", "[cli][wall]") {
    const Sample sample("curve");
    drawRoomSquare(sample);
    const auto outcome = sample.run({"wall", "set", "w1", "--bulge", "-2"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& wall = outcome.answer.at("result");
    CHECK(wall.at("bulge") == -2.0);
    CHECK_THAT(wall.at("length").get<double>(), WithinAbs(6.2832, 1e-3));
    CHECK_THAT(wall.at("radius").get<double>(), WithinAbs(2.0, 1e-9));
    CHECK_THAT(wall.at("height_used").get<double>(), WithinAbs(3.0, 1e-12));
    CHECK(wall.at("openings").empty());
}

TEST_CASE("a straight wall has a null radius and its chord as length", "[cli][wall]") {
    const Sample sample("straight");
    drawRoomSquare(sample);
    const auto list = sample.run({"wall", "list"});
    const auto& walls = list.answer.at("result").at("walls");
    REQUIRE(walls.size() == 4);
    CHECK(walls.at(0).at("radius").is_null());
    CHECK_THAT(walls.at(0).at("length").get<double>(), WithinAbs(4.0, 1e-9));
}

TEST_CASE("point set rounds a corner and the room area follows the arc", "[cli][point]") {
    const Sample sample("round");
    drawRoomSquare(sample);
    REQUIRE(sample.run({"wall", "set", "w1", "--bulge", "-2"}).exitCode == kExitOk);
    const auto rounded = sample.run({"point", "set", "p3", "--corner-radius", "1"});
    REQUIRE(rounded.exitCode == kExitOk);
    CHECK(rounded.answer.at("result").at("corner_radius") == 1.0);
    CHECK(rounded.answer.at("result").at("corner_radius_used") == 1.0);

    const auto rooms = sample.run({"room", "list"});
    REQUIRE(rooms.exitCode == kExitOk);
    CHECK_THAT(rooms.answer.at("result").at("rooms").at(0).at("area").get<double>(), WithinAbs(20.32, 0.05));
}

TEST_CASE("a corner radius on a point with one wall is kept but not used", "[cli][point]") {
    const Sample sample("notused");
    drawRoomSquare(sample);
    REQUIRE(sample.run({"point", "add", "--x", "9", "--y", "9"}).exitCode == kExitOk);
    const auto outcome = sample.run({"point", "set", "p5", "--corner-radius", "0.5"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("corner_radius") == 0.5);
    CHECK(outcome.answer.at("result").at("corner_radius_used") == 0.0);
}

TEST_CASE("point set refuses a negative radius and leaves the plan unchanged", "[cli][point]") {
    const Sample sample("negradius");
    drawRoomSquare(sample);
    const auto before = readFile(sample.folder / "plan.json");
    const auto outcome = sample.run({"point", "set", "p3", "--corner-radius", "-0.1"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(sample.run({"point", "set", "p3"}).exitCode == kExitBadArguments);
}

TEST_CASE("wall set refuses a bulge past a half circle and leaves the plan unchanged", "[cli][wall]") {
    const Sample sample("bigbulge");
    drawRoomSquare(sample);
    const auto before = readFile(sample.folder / "plan.json");
    const auto outcome = sample.run({"wall", "set", "w1", "--bulge", "3"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("wall set with a bulge of 0 straightens a curved wall", "[cli][wall]") {
    const Sample sample("straighten");
    drawRoomSquare(sample);
    REQUIRE(sample.run({"wall", "set", "w1", "--bulge", "1"}).exitCode == kExitOk);
    const auto outcome = sample.run({"wall", "set", "w1", "--bulge", "0"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("bulge") == 0.0);
    CHECK(outcome.answer.at("result").at("radius").is_null());
}

TEST_CASE("wall set with several options applies all or none", "[cli][wall]") {
    const Sample sample("group");
    drawRoomSquare(sample);
    const auto before = readFile(sample.folder / "plan.json");
    const auto refused = sample.run({"wall", "set", "w1", "--thickness", "0.3", "--bulge", "9"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto both = sample.run({"wall", "set", "w1", "--thickness", "0.3", "--bulge", "0.5"});
    REQUIRE(both.exitCode == kExitOk);
    CHECK(both.answer.at("result").at("thickness") == 0.3);
    CHECK(both.answer.at("result").at("bulge") == 0.5);
}

TEST_CASE("wall set without an option is bad_arguments", "[cli][wall]") {
    const Sample sample("setnone");
    drawRoomSquare(sample);
    const auto outcome = sample.run({"wall", "set", "w1"});
    CHECK(outcome.exitCode == kExitBadArguments);
    CHECK(errorCode(outcome) == "bad_arguments");
}

TEST_CASE("wall draw with a snap onto a curved wall splits it", "[cli][wall]") {
    const Sample sample("splitcurve");
    drawRoomSquare(sample);
    REQUIRE(sample.run({"wall", "set", "w1", "--bulge", "-2"}).exitCode == kExitOk);
    const auto outcome = sample.draw({"--from-x", "3", "--from-y", "1.04", "--snap", "0.1", "--to-x", "3", "--to-y", "0"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(ids(result.at("split_walls")) == std::vector<std::string>{"w1"});
    CHECK(result.at("new_points").size() == 2);
    const auto walls = sample.run({"wall", "list"});
    const auto& list = walls.answer.at("result").at("walls");
    int curved = 0;
    for (const auto& wall : list) {
        if (wall.at("radius").is_number()) {
            ++curved;
            CHECK_THAT(wall.at("radius").get<double>(), WithinAbs(2.0, 1e-6));
        }
    }
    CHECK(curved == 2);
}
