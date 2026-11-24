#include "FinishLibrary.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::FinishSample;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;

namespace {

// The saved opening with this id, or an empty object when there is none.
nlohmann::json savedOpening(const Sample& sample, const std::string& id) {
    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    nlohmann::json found = nlohmann::json::object();
    for (const auto& opening : plan.at("floors").at(0).at("openings")) {
        if (opening.at("id") == id) {
            found = opening;
        }
    }
    return found;
}

}

TEST_CASE("room set turns set dressing off and on and keeps the type", "[cli][finish]") {
    Sample sample("room-dressing");
    sample.drawRoomSquare();
    const auto listed = sample.run({"room", "list"});
    CHECK(listed.answer.at("result").at("rooms").at(0).at("dressing") == true);

    const auto off = sample.run({"room", "set", "r1", "--dressing", "off"});
    REQUIRE(off.exitCode == kExitOk);
    CHECK(off.answer.at("command") == "room set");
    CHECK(off.answer.at("result").at("dressing") == false);
    CHECK(off.answer.at("result").at("type") == "living");
    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    CHECK(plan.at("floors").at(0).at("rooms").at(0).at("dressing") == false);

    const auto both = sample.run({"room", "set", "r1", "--dressing", "on", "--type", "kitchen"});
    REQUIRE(both.exitCode == kExitOk);
    CHECK(both.answer.at("result").at("dressing") == true);
    CHECK(both.answer.at("result").at("type") == "kitchen");
}

TEST_CASE("room set refuses a bad switch, no option, an unknown room and a bad type with nothing changed",
          "[cli][finish]") {
    Sample sample("room-dressing-refuse");
    sample.drawRoomSquare();
    const std::string before = readFile(sample.folder / "plan.json");

    const auto none = sample.run({"room", "set", "r1"});
    CHECK(none.exitCode == kExitBadArguments);
    CHECK(errorCode(none) == "bad_arguments");
    CHECK(errorCode(sample.run({"room", "set", "r1", "--dressing", "maybe"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"room", "set", "r9", "--dressing", "off"})) == "not_found");
    const auto type = sample.run({"room", "set", "r1", "--dressing", "off", "--type", "dungeon"});
    CHECK(type.exitCode == kExitFailed);
    CHECK(errorCode(type) == "invalid_value");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("opening entries say whether the build picks a door or window", "[cli][finish]") {
    Sample sample("opening-auto-frame");
    sample.drawRoomSquare();
    const auto added = sample.run({"opening", "add", "--wall", "w2", "--offset", "2"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("auto_frame") == true);
    CHECK(added.answer.at("result").at("frame").is_null());
}

TEST_CASE("opening set --frame generated and auto switch picking without a library", "[cli][finish]") {
    plynth::cli::testing::GuardedSettingsReset reset;
    Sample sample("opening-frame-switch");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2"}).exitCode == kExitOk);

    const auto generated = sample.run({"opening", "set", "o1", "--frame", "generated"});
    REQUIRE(generated.exitCode == kExitOk);
    CHECK(generated.answer.at("command") == "opening set");
    CHECK(generated.answer.at("result").at("auto_frame") == false);
    CHECK(generated.answer.at("result").at("frame").is_null());
    CHECK(savedOpening(sample, "o1").at("auto_frame") == false);

    const auto automatic = sample.run({"opening", "set", "o1", "--frame", "auto"});
    REQUIRE(automatic.exitCode == kExitOk);
    CHECK(automatic.answer.at("result").at("auto_frame") == true);
    CHECK(savedOpening(sample, "o1").at("auto_frame") == true);
}

TEST_CASE("opening set --frame names a catalog door or window", "[cli][finish]") {
    const FinishSample finish("opening-frame-asset");
    const auto door = finish.run({"opening", "set", "o1", "--frame", "oak_door"});
    REQUIRE(door.exitCode == kExitOk);
    CHECK(door.answer.at("result").at("frame") == "oak_door");
    CHECK(door.answer.at("result").at("auto_frame") == false);
    CHECK(savedOpening(finish.sample, "o1").at("frame") == "oak_door");

    const auto window = finish.run({"opening", "set", "o2", "--frame", "small_window"});
    REQUIRE(window.exitCode == kExitOk);
    CHECK(window.answer.at("result").at("frame") == "small_window");

    const auto back = finish.run({"opening", "set", "o1", "--frame", "auto"});
    REQUIRE(back.exitCode == kExitOk);
    CHECK(back.answer.at("result").at("frame").is_null());
    CHECK(back.answer.at("result").at("auto_frame") == true);

    const auto styled = finish.run({"opening", "set", "o1", "--swing", "left", "--frame", "oak_door"});
    REQUIRE(styled.exitCode == kExitOk);
    CHECK(styled.answer.at("result").at("swing") == "left");
    CHECK(styled.answer.at("result").at("frame") == "oak_door");
}

TEST_CASE("opening set --frame refuses unknown assets, other categories and plain openings with nothing changed",
          "[cli][finish]") {
    const FinishSample finish("opening-frame-refuse");
    REQUIRE(finish.run({"opening", "add", "--wall", "w4", "--offset", "2", "--type", "plain"}).exitCode == kExitOk);
    const std::string before = readFile(finish.sample.folder / "plan.json");

    const auto unknown = finish.run({"opening", "set", "o1", "--frame", "iron_gate"});
    CHECK(unknown.exitCode == kExitFailed);
    CHECK(errorCode(unknown) == "not_found");
    const auto window = finish.run({"opening", "set", "o1", "--frame", "small_window"});
    CHECK(errorCode(window) == "invalid_value");
    const auto sofa = finish.run({"opening", "set", "o2", "--frame", "sofa_a"});
    CHECK(errorCode(sofa) == "invalid_value");
    const auto plain = finish.run({"opening", "set", "o3", "--frame", "oak_door"});
    CHECK(errorCode(plain) == "invalid_value");
    const auto badId = finish.run({"opening", "set", "o1", "--frame", "../door"});
    CHECK(errorCode(badId) == "invalid_value");
    const auto noOpening = finish.run({"opening", "set", "o9", "--frame", "oak_door"});
    CHECK(errorCode(noOpening) == "not_found");
    const auto typeChange = finish.run({"opening", "set", "o1", "--type", "window", "--frame", "oak_door"});
    CHECK(errorCode(typeChange) == "invalid_value");

    CHECK(readFile(finish.sample.folder / "plan.json") == before);
}

TEST_CASE("room set and opening set --frame follow the lock rules", "[cli][finish]") {
    Sample sample("finish-lock");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2"}).exitCode == kExitOk);
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const std::string before = readFile(sample.folder / "plan.json");

    CHECK(errorCode(sample.run({"room", "set", "r1", "--dressing", "off"})) == "locked");
    CHECK(errorCode(sample.run({"opening", "set", "o1", "--frame", "generated"})) == "locked");
    CHECK(readFile(sample.folder / "plan.json") == before);
}
