#include "SampleProject.h"

#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace plynth::cli;
using namespace plynth::cli::testing;
using Catch::Matchers::WithinAbs;

namespace {

// The m5b project up to the three good openings: the room square with a half circle on w1 and a rounded p3,
// then a door o1 on w2, a round arch window o2 on w3 and a plain pointed arch opening o3 on the arc of w1.
void buildOpenings(const Sample& sample) {
    sample.drawRoomSquare();
    REQUIRE(sample.run({"wall", "set", "w1", "--bulge", "-2"}).exitCode == kExitOk);
    REQUIRE(sample.run({"point", "set", "p3", "--corner-radius", "1"}).exitCode == kExitOk);
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--type", "door", "--swing", "right"})
                .exitCode == kExitOk);
    REQUIRE(sample.run({"opening", "add", "--wall", "w3", "--offset", "2", "--type", "window", "--shape", "round_arch"})
                .exitCode == kExitOk);
    REQUIRE(sample.run({"opening", "add", "--wall", "w1", "--offset", "3.1416", "--type", "plain", "--shape",
                        "pointed_arch", "--width", "1", "--height", "2.4"})
                .exitCode == kExitOk);
}

}

TEST_CASE("opening add puts a door on a wall with the project's default size", "[cli][opening]") {
    const Sample sample("op-door");
    sample.drawRoomSquare();
    const auto outcome = sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--swing", "right"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& o = outcome.answer.at("result");
    CHECK(o.at("id") == "o1");
    CHECK(o.at("floor") == "f1");
    CHECK(o.at("wall") == "w2");
    CHECK(o.at("offset") == 2.0);
    CHECK(o.at("type") == "door");
    CHECK(o.at("shape") == "rectangle");
    CHECK(o.at("width") == 0.9);
    CHECK(o.at("height") == 2.1);
    CHECK(o.at("sill") == 0.0);
    CHECK(o.at("hinge") == "start");
    CHECK(o.at("swing") == "right");
    CHECK(o.at("frame").is_null());
    CHECK_THAT(o.at("centre").at("x").get<double>(), WithinAbs(5.0, 1e-9));
    CHECK_THAT(o.at("centre").at("y").get<double>(), WithinAbs(5.0, 1e-9));
    CHECK(o.at("fits") == true);
    CHECK(o.at("problem").is_null());
}

TEST_CASE("the m5b script gives the listed answers", "[cli][opening]") {
    const Sample sample("op-script");
    buildOpenings(sample);

    const auto window = sample.run({"opening", "list", "--wall", "w3"});
    const auto& o2 = window.answer.at("result").at("openings").at(0);
    CHECK(o2.at("id") == "o2");
    CHECK(o2.at("shape") == "round_arch");
    CHECK(o2.at("sill") == 0.9);
    CHECK(o2.at("width") == 1.2);
    CHECK(o2.at("height") == 1.2);

    const auto arc = sample.run({"opening", "list", "--wall", "w1"});
    const auto& o3 = arc.answer.at("result").at("openings").at(0);
    CHECK(o3.at("id") == "o3");
    CHECK(o3.at("type") == "plain");
    CHECK(o3.at("shape") == "pointed_arch");
    CHECK_THAT(o3.at("centre").at("x").get<double>(), WithinAbs(3.0, 1e-3));
    CHECK_THAT(o3.at("centre").at("y").get<double>(), WithinAbs(1.0, 1e-3));

    const auto before = readFile(sample.folder / "plan.json");
    const auto past = sample.run({"opening", "add", "--wall", "w4", "--offset", "2", "--type", "door", "--width", "3.9"});
    CHECK(past.exitCode == kExitFailed);
    CHECK(errorCode(past) == "invalid_value");
    const auto overlap = sample.run({"opening", "add", "--wall", "w2", "--offset", "2.2", "--type", "window"});
    CHECK(overlap.exitCode == kExitFailed);
    CHECK(errorCode(overlap) == "invalid_value");
    CHECK(overlap.answer.at("error").at("message").get<std::string>().find("o1") != std::string::npos);
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto byPlace = sample.run({"opening", "add", "--x", "1.05", "--y", "5", "--type", "window", "--step", "0.05"});
    REQUIRE(byPlace.exitCode == kExitOk);
    CHECK(byPlace.answer.at("result").at("id") == "o4");
    CHECK(byPlace.answer.at("result").at("wall") == "w4");
    CHECK_THAT(byPlace.answer.at("result").at("offset").get<double>(), WithinAbs(2.0, 1e-9));

    const auto removed = sample.run({"opening", "remove", "o4"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") ==
          nlohmann::json::parse(R"({"openings":["o4"],"rooms":[],"walls":[],"points":[]})"));

    const auto byWall = sample.run({"opening", "add", "--wall", "w4", "--offset", "2", "--type", "window"});
    REQUIRE(byWall.exitCode == kExitOk);
    auto a = byWall.answer.at("result");
    auto b = byPlace.answer.at("result");
    a.erase("id");
    b.erase("id");
    CHECK(a == b);
    REQUIRE(sample.run({"opening", "remove", byWall.answer.at("result").at("id").get<std::string>()}).exitCode ==
            kExitOk);

    REQUIRE(sample.run({"wall", "set", "w4", "--height", "1.1"}).exitCode == kExitOk);
    const auto tall = sample.run({"opening", "add", "--wall", "w4", "--offset", "2", "--type", "window"});
    CHECK(tall.exitCode == kExitFailed);
    CHECK(errorCode(tall) == "invalid_value");

    const auto low = sample.run({"floor", "set", "--ceiling-height", "2.0"});
    CHECK(low.exitCode == kExitFailed);
    CHECK(errorCode(low) == "invalid_value");
    CHECK(low.answer.at("error").at("message").get<std::string>().find("o1") != std::string::npos);

    const auto fine = sample.run({"floor", "set", "--ceiling-height", "2.8"});
    REQUIRE(fine.exitCode == kExitOk);
    CHECK(fine.answer.at("result").at("ceiling_height") == 2.8);
    CHECK(fine.answer.at("result").at("id") == "f1");

    const auto list = sample.run({"opening", "list"});
    REQUIRE(list.exitCode == kExitOk);
    CHECK(list.answer.at("result").at("floor") == "f1");
    CHECK(list.answer.at("result").at("openings").size() == 3);
    CHECK(list.answer.at("result").at("problems") == 0);

    const auto walls = sample.run({"wall", "list"});
    const auto& w4 = walls.answer.at("result").at("walls").at(3);
    CHECK(w4.at("height") == 1.1);
    CHECK(w4.at("height_used") == 1.1);
    const auto& w2 = walls.answer.at("result").at("walls").at(1);
    CHECK(ids(w2.at("openings")) == std::vector<std::string>{"o1"});
}

TEST_CASE("opening add with both placement forms or neither is bad_arguments", "[cli][opening]") {
    const Sample sample("op-forms");
    sample.drawRoomSquare();
    const auto before = readFile(sample.folder / "plan.json");
    const std::vector<std::vector<std::string>> bad = {
        {"opening", "add", "--wall", "w2", "--offset", "1", "--x", "5", "--y", "5"},
        {"opening", "add"},
        {"opening", "add", "--wall", "w2"},
        {"opening", "add", "--offset", "1"},
        {"opening", "add", "--x", "5"},
    };
    for (const auto& args : bad) {
        const auto outcome = sample.run(args);
        CHECK(outcome.exitCode == kExitBadArguments);
        CHECK(errorCode(outcome) == "bad_arguments");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("opening add refuses an unknown shape and lists the ids", "[cli][opening]") {
    const Sample sample("op-shape");
    sample.drawRoomSquare();
    const auto before = readFile(sample.folder / "plan.json");
    const auto outcome = sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--shape", "bogus"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(outcome.answer.at("error").at("message").get<std::string>().find("round_arch") != std::string::npos);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("opening add by place finds no wall far away and uses the flat's defaults", "[cli][opening]") {
    const Sample sample("op-place");
    sample.drawRoomSquare();
    const auto far = sample.run({"opening", "add", "--x", "20", "--y", "20"});
    CHECK(far.exitCode == kExitFailed);
    CHECK(errorCode(far) == "not_found");
    REQUIRE(sample.run({"project", "set", "--door-width", "1.0"}).exitCode == kExitOk);
    const auto near = sample.run({"opening", "add", "--x", "3", "--y", "3.05"});
    REQUIRE(near.exitCode == kExitOk);
    CHECK(near.answer.at("result").at("wall") == "w1");
    CHECK(near.answer.at("result").at("width") == 1.0);
}

TEST_CASE("opening move, resize and set change what they name and keep the rest", "[cli][opening]") {
    const Sample sample("op-edit");
    buildOpenings(sample);

    const auto moved = sample.run({"opening", "move", "o1", "--offset", "1.5"});
    REQUIRE(moved.exitCode == kExitOk);
    CHECK(moved.answer.at("result").at("offset") == 1.5);
    CHECK(moved.answer.at("result").at("wall") == "w2");
    const auto onto = sample.run({"opening", "move", "o1", "--wall", "w3", "--offset", "3.4"});
    REQUIRE(onto.exitCode == kExitOk);
    CHECK(onto.answer.at("result").at("wall") == "w3");
    CHECK(onto.answer.at("result").at("width") == 0.9);
    CHECK(sample.run({"opening", "move", "o1"}).exitCode == kExitBadArguments);

    const auto before = readFile(sample.folder / "plan.json");
    const auto clash = sample.run({"opening", "move", "o1", "--offset", "2"});
    CHECK(clash.exitCode == kExitFailed);
    CHECK(errorCode(clash) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto resized = sample.run({"opening", "resize", "o2", "--width", "1.0"});
    REQUIRE(resized.exitCode == kExitOk);
    CHECK(resized.answer.at("result").at("width") == 1.0);
    CHECK(resized.answer.at("result").at("height") == 1.2);
    CHECK(resized.answer.at("result").at("offset") == 2.0);
    CHECK(sample.run({"opening", "resize", "o2"}).exitCode == kExitBadArguments);
    const auto tall = sample.run({"opening", "resize", "o2", "--height", "3.5"});
    CHECK(tall.exitCode == kExitFailed);
    CHECK(errorCode(tall) == "invalid_value");

    const auto styled = sample.run({"opening", "set", "o1", "--swing", "left", "--hinge", "end"});
    REQUIRE(styled.exitCode == kExitOk);
    CHECK(styled.answer.at("result").at("swing") == "left");
    CHECK(styled.answer.at("result").at("hinge") == "end");
    CHECK(styled.answer.at("result").at("type") == "door");
    const auto shaped = sample.run({"opening", "set", "o2", "--type", "door", "--shape", "pointed_arch"});
    REQUIRE(shaped.exitCode == kExitOk);
    CHECK(shaped.answer.at("result").at("type") == "door");
    CHECK(sample.run({"opening", "set", "o2"}).exitCode == kExitBadArguments);
    CHECK(errorCode(sample.run({"opening", "set", "o2", "--shape", "bogus"})) == "invalid_value");
    CHECK(errorCode(sample.run({"opening", "set", "o9", "--type", "window"})) == "not_found");
    CHECK(errorCode(sample.run({"opening", "move", "o9", "--sill", "0"})) == "not_found");
    CHECK(errorCode(sample.run({"opening", "resize", "o9", "--width", "1"})) == "not_found");
}

TEST_CASE("wall set accepts a height or none and refuses one below an opening", "[cli][wall]") {
    const Sample sample("op-height");
    buildOpenings(sample);
    const auto before = readFile(sample.folder / "plan.json");
    const auto refused = sample.run({"wall", "set", "w2", "--height", "1.1"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(sample.run({"wall", "set", "w2", "--height", "abc"}).exitCode == kExitBadArguments);

    const auto half = sample.run({"wall", "set", "w4", "--height", "1.1"});
    REQUIRE(half.exitCode == kExitOk);
    CHECK(half.answer.at("result").at("height") == 1.1);
    const auto cleared = sample.run({"wall", "set", "w4", "--height", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("height").is_null());
    CHECK(cleared.answer.at("result").at("height_used") == 3.0);
}

TEST_CASE("floor set refuses a ceiling below an opening and names a missing floor", "[cli][floor]") {
    const Sample sample("op-floor");
    buildOpenings(sample);
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"floor", "set", "--ceiling-height", "2.0"})) == "invalid_value");
    CHECK(errorCode(sample.run({"floor", "set", "--floor", "f9", "--ceiling-height", "3"})) == "not_found");
    CHECK(sample.run({"floor", "set"}).exitCode == kExitBadArguments);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("wall remove needs cascade for a wall with openings and then lists them", "[cli][opening]") {
    const Sample sample("op-cascade");
    buildOpenings(sample);
    const auto before = readFile(sample.folder / "plan.json");
    const auto plain = sample.run({"wall", "remove", "w2"});
    CHECK(plain.exitCode == kExitFailed);
    CHECK(errorCode(plain) == "in_use");
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto copy = sample.temp.path() / "copy";
    std::filesystem::copy(sample.folder, copy, std::filesystem::copy_options::recursive);
    const auto cascade = runChecked({"wall", "remove", "w2", "--cascade", "--project", utf8(copy)});
    REQUIRE(cascade.exitCode == kExitOk);
    const auto& removed = cascade.answer.at("result").at("removed");
    CHECK(ids(removed.at("openings")) == std::vector<std::string>{"o1"});
    CHECK(ids(removed.at("walls")).front() == "w2");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("opening remove refuses an id that is not an opening", "[cli][opening]") {
    const Sample sample("op-remove");
    buildOpenings(sample);
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"opening", "remove", "w2"})) == "invalid_value");
    CHECK(errorCode(sample.run({"opening", "remove", "o9"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("opening list reports openings that stopped fitting", "[cli][opening]") {
    const Sample sample("op-problems");
    buildOpenings(sample);
    REQUIRE(sample.run({"point", "move", "p3", "--x", "5", "--y", "3.5"}).exitCode == kExitOk);
    const auto list = sample.run({"opening", "list"});
    REQUIRE(list.exitCode == kExitOk);
    const auto& result = list.answer.at("result");
    CHECK(result.at("problems").get<int>() >= 1);
    for (const auto& opening : result.at("openings")) {
        CHECK(opening.at("fits").get<bool>() == opening.at("problem").is_null());
    }
    CHECK(errorCode(sample.run({"opening", "list", "--wall", "w99"})) == "not_found");
}

TEST_CASE("opening edits are refused with locked while the app has the project open and lists are not",
          "[cli][opening]") {
    const Sample sample("op-locked");
    buildOpenings(sample);
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const auto before = readFile(sample.folder / "plan.json");

    const std::vector<std::vector<std::string>> edits = {
        {"opening", "add", "--wall", "w2", "--offset", "3"},
        {"opening", "move", "o1", "--offset", "1.5"},
        {"opening", "resize", "o1", "--width", "0.8"},
        {"opening", "set", "o1", "--swing", "left"},
        {"opening", "remove", "o1"},
        {"floor", "set", "--ceiling-height", "2.9"},
        {"wall", "set", "w4", "--height", "1.1"},
        {"project", "set", "--door-height", "2.0"},
    };
    for (const auto& args : edits) {
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);

    CHECK(sample.run({"opening", "list"}).exitCode == kExitOk);
    CHECK(sample.run({"floor", "list"}).exitCode == kExitOk);
    CHECK(runChecked({"opening", "shapes"}).exitCode == kExitOk);
}

TEST_CASE("the opening and floor commands are listed by help", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"opening add", "opening move", "opening resize", "opening set", "opening remove",
                               "opening list", "opening shapes", "floor list", "floor set"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
}
