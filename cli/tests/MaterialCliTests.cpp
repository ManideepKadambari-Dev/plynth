#include "SampleProject.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

namespace {

// A 6 by 4 m building split by a wall at x 4: walls w1 to w4 outside, w5 inside, rooms r1 (left) and r2
// (right) with the given types.
void buildTwoRooms(const Sample& sample, const std::string& leftType, const std::string& rightType) {
    REQUIRE(sample.run({"scale", "from-points", "--x1", "100", "--y1", "100", "--x2", "600", "--y2", "100",
                        "--distance", "5"})
                .exitCode == kExitOk);
    const std::vector<std::vector<std::string>> steps = {
        {"--from-x", "1", "--from-y", "1", "--to-x", "7", "--to-y", "1", "--thickness", "0.3"},
        {"--from", "p2", "--to-x", "7", "--to-y", "5", "--thickness", "0.3"},
        {"--from", "p3", "--to-x", "1", "--to-y", "5", "--thickness", "0.3"},
        {"--from", "p4", "--to", "p1", "--thickness", "0.3"},
        {"--from-x", "4", "--from-y", "1", "--to-x", "4", "--to-y", "5", "--snap", "0.1", "--thickness", "0.3"},
    };
    for (const auto& step : steps) {
        REQUIRE(sample.draw(step).exitCode == kExitOk);
    }
    REQUIRE(sample.run({"room", "add", "--x", "2.5", "--y", "3", "--type", leftType}).exitCode == kExitOk);
    REQUIRE(sample.run({"room", "add", "--x", "5.5", "--y", "3", "--type", rightType}).exitCode == kExitOk);
}

// The entry of an id in an array of objects with an "id" field.
const nlohmann::json& entryOf(const nlohmann::json& array, const std::string& id) {
    for (const auto& entry : array) {
        if (entry.at("id") == id) {
            return entry;
        }
    }
    FAIL("no entry " << id);
    return array;
}

nlohmann::json projectJson(const Sample& sample) {
    return nlohmann::json::parse(readFile(sample.folder / "project.json"));
}

nlohmann::json planJson(const Sample& sample) {
    return nlohmann::json::parse(readFile(sample.folder / "plan.json"));
}

// The wall with an id in the first floor of the plan.
nlohmann::json wallOf(const Sample& sample, const std::string& id) {
    const auto plan = planJson(sample);
    for (const auto& wall : plan.at("floors").at(0).at("walls")) {
        if (wall.at("id") == id) {
            return wall;
        }
    }
    FAIL("no wall " << id);
    return {};
}

}

TEST_CASE("material set changes a room, a wall face and a default", "[cli][material]") {
    Sample sample("material-set");
    buildTwoRooms(sample, "living", "hall");

    const auto floor = sample.run({"material", "set", "r1.floor", "oak_planks"});
    REQUIRE(floor.exitCode == kExitOk);
    const auto& result = floor.answer.at("result");
    CHECK(result.at("target") == "r1.floor");
    CHECK(result.at("material") == "oak_planks");
    CHECK(result.at("resolved").at("origin") == "room");
    CHECK(result.at("resolved").at("from") == "r1");
    const auto plan = planJson(sample);
    const auto& rooms = plan.at("floors").at(0).at("rooms");
    CHECK(entryOf(rooms, "r1").at("floor_material") == "oak_planks");

    const auto outside = sample.run({"material", "set", "w3.outside", "red_brick"});
    REQUIRE(outside.exitCode == kExitOk);
    CHECK(outside.answer.at("result").at("previous").is_null());
    CHECK(wallOf(sample, "w3").at("material_outside") == "red_brick");

    const auto cleared = sample.run({"material", "set", "w3.outside", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("material").is_null());
    CHECK(cleared.answer.at("result").at("previous") == "red_brick");
    CHECK(cleared.answer.at("result").at("resolved").at("origin") == "default");
    CHECK(wallOf(sample, "w3").at("material_outside").is_null());

    const auto fallback = sample.run({"material", "set", "default.wall_outside", "stone_wall"});
    REQUIRE(fallback.exitCode == kExitOk);
    CHECK(fallback.answer.at("result").at("previous") == "red_brick");
    CHECK(projectJson(sample).at("materials").at("wall_outside") == "stone_wall");

    const auto none = sample.run({"material", "set", "default.glass", "none"});
    REQUIRE(none.exitCode == kExitOk);
    CHECK(none.answer.at("result").at("resolved").at("origin") == "none");
}

TEST_CASE("material set takes a room type when one room has it", "[cli][material]") {
    Sample sample("material-hall");
    buildTwoRooms(sample, "living", "hall");
    const auto set = sample.run({"material", "set", "hall.floor", "oak_planks"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("resolved").at("from") == "r2");
}

TEST_CASE("material set names both rooms when a type matches two", "[cli][material]") {
    Sample sample("material-halls");
    buildTwoRooms(sample, "hall", "hall");
    const auto before = readFile(sample.folder / "plan.json");
    const auto set = sample.run({"material", "set", "hall.floor", "oak_planks"});
    CHECK(set.exitCode == kExitFailed);
    CHECK(errorCode(set) == "invalid_value");
    const std::string message = set.answer.at("error").at("message");
    CHECK(message.find("r1") != std::string::npos);
    CHECK(message.find("r2") != std::string::npos);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("material set refuses what is wrong and changes nothing", "[cli][material]") {
    Sample sample("material-refuse");
    buildTwoRooms(sample, "living", "hall");
    const auto plan = readFile(sample.folder / "plan.json");
    const auto project = readFile(sample.folder / "project.json");

    const auto unknownWall = sample.run({"material", "set", "w99.inside", "oak_planks"});
    CHECK(unknownWall.exitCode == kExitFailed);
    CHECK(errorCode(unknownWall) == "not_found");
    const auto unknownRoom = sample.run({"material", "set", "r99.floor", "oak_planks"});
    CHECK(errorCode(unknownRoom) == "not_found");
    const auto badTarget = sample.run({"material", "set", "r1.roof", "oak_planks"});
    CHECK(errorCode(badTarget) == "invalid_value");
    const auto badSurface = sample.run({"material", "set", "default.roof", "oak_planks"});
    CHECK(errorCode(badSurface) == "invalid_value");
    const auto badName = sample.run({"material", "set", "r1.floor", "../oak planks"});
    CHECK(badName.exitCode == kExitFailed);
    CHECK(errorCode(badName) == "invalid_value");

    CHECK(readFile(sample.folder / "plan.json") == plan);
    CHECK(readFile(sample.folder / "project.json") == project);
}

TEST_CASE("material set is refused with locked while the app has the project open", "[cli][material]") {
    Sample sample("material-locked");
    buildTwoRooms(sample, "living", "hall");

    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const auto plan = readFile(sample.folder / "plan.json");
    const auto project = readFile(sample.folder / "project.json");

    for (const char* target : {"r1.floor", "w1.inside", "default.floor"}) {
        const auto refused = sample.run({"material", "set", target, "oak_planks"});
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == plan);
    CHECK(readFile(sample.folder / "project.json") == project);
    CHECK(sample.run({"material", "list"}).exitCode == kExitOk);
}

TEST_CASE("material list gives resolved materials with origins and the ones used", "[cli][material]") {
    Sample sample("material-list");
    buildTwoRooms(sample, "living", "hall");
    REQUIRE(sample.run({"material", "set", "r1.floor", "oak_planks"}).exitCode == kExitOk);
    REQUIRE(sample.run({"material", "set", "r1.wall", "sage_paint"}).exitCode == kExitOk);
    std::string innerId;
    const auto before = sample.run({"material", "list"});
    for (const auto& wall : before.answer.at("result").at("walls")) {
        if (wall.at("inside").size() == 2) {
            innerId = wall.at("id");
        }
    }
    REQUIRE_FALSE(innerId.empty());
    REQUIRE(sample.run({"material", "set", innerId + ".inside", "red_brick"}).exitCode == kExitOk);
    REQUIRE(sample.run({"material", "set", "default.floor", "grey_tile"}).exitCode == kExitOk);

    const auto listed = sample.run({"material", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    const auto& result = listed.answer.at("result");
    CHECK(result.at("defaults").at("floor") == "grey_tile");
    CHECK(result.at("defaults").at("wall_inside") == "white_paint");
    CHECK(result.at("defaults").size() == 9);
    CHECK(result.at("missing").empty());
    CHECK(result.at("library").is_null());

    const auto& rooms = result.at("rooms");
    REQUIRE(rooms.size() == 2);
    const auto& r1 = entryOf(rooms, "r1").at("materials");
    CHECK(r1.at("floor").at("material") == "oak_planks");
    CHECK(r1.at("floor").at("origin") == "room");
    CHECK(r1.at("floor").at("from") == "r1");
    CHECK(r1.at("wall").at("material") == "sage_paint");
    CHECK(r1.at("ceiling").at("origin") == "default");
    CHECK(r1.at("ceiling").at("from").is_null());
    const auto& r2 = entryOf(rooms, "r2").at("materials");
    CHECK(r2.at("floor").at("material") == "grey_tile");
    CHECK(r2.at("floor").at("origin") == "default");

    const auto& walls = result.at("walls");
    REQUIRE(walls.size() == 7);
    const auto& inner = entryOf(walls, innerId);
    REQUIRE(inner.at("inside").size() == 2);
    for (const auto& face : inner.at("inside")) {
        CHECK(face.at("material") == "red_brick");
        CHECK(face.at("origin") == "wall");
        CHECK(face.at("from") == innerId);
    }
    const auto& outer = entryOf(walls, "w1");
    CHECK(outer.at("floor") == "f1");
    CHECK(outer.at("outside").at("material") == "red_brick");
    CHECK(outer.at("outside").at("origin") == "default");
    REQUIRE(outer.at("inside").size() == 1);
    CHECK(outer.at("inside").at(0).at("room") == "r1");

    std::vector<std::string> used = result.at("used").get<std::vector<std::string>>();
    CHECK(std::ranges::is_sorted(used));
    for (const char* id : {"oak_planks", "sage_paint", "red_brick", "grey_tile", "white_paint"}) {
        CHECK(std::ranges::find(used, id) != used.end());
    }
}

TEST_CASE("material list works on a project with no rooms", "[cli][material]") {
    Sample sample("material-empty");
    const auto listed = sample.run({"material", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    CHECK(listed.answer.at("result").at("walls").empty());
    CHECK(listed.answer.at("result").at("rooms").empty());
    CHECK(listed.answer.at("result").at("defaults").at("wall_inside") == "white_paint");
}

TEST_CASE("help lists the material commands", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"material set", "material list"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
}
