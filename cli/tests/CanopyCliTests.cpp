#include "MaterialLibrary.h"
#include "SampleProject.h"

#include <plynth/cli/Cli.h>
#include <plynth/cli/Words.h>
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
using plynth::cli::testing::MaterialLibrary;
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

// The codes in a list of problems.
std::vector<std::string> codesOf(const nlohmann::json& problems) {
    std::vector<std::string> codes;
    for (const auto& problem : problems) {
        codes.push_back(problem.at("code").get<std::string>());
    }
    return codes;
}

// Says whether a list of codes holds one.
bool holds(const std::vector<std::string>& codes, const std::string& code) {
    return std::ranges::find(codes, code) != codes.end();
}

// The porch next to the right wall of the room square: 3 m by 4 m, touching the wall at x 5.
const std::vector<std::string> kPorch = {"--points", "5,3", "8,3", "8,7", "5,7"};

// Runs canopy add on the sample with the porch outline and the extra options.
plynth::cli::testing::Outcome addPorch(const Sample& sample, const std::vector<std::string>& extra = {}) {
    std::vector<std::string> args = {"canopy", "add"};
    args.insert(args.end(), kPorch.begin(), kPorch.end());
    args.insert(args.end(), extra.begin(), extra.end());
    return sample.run(args);
}

// Draws a closed rectangle of walls on a floor, from (x0, y0) to (x1, y1), with a room in it.
void drawBox(const Sample& sample, const std::string& floor, double x0, double y0, double x1, double y1) {
    const auto text = [](double v) { return std::to_string(v); };
    const auto first = sample.draw({"--from-x", text(x0), "--from-y", text(y0), "--to-x", text(x1), "--to-y", text(y0),
                                    "--floor", floor});
    REQUIRE(first.exitCode == kExitOk);
    const std::string start = first.answer.at("result").at("from");
    const std::string a = first.answer.at("result").at("to");
    const auto second = sample.draw({"--from", a, "--to-x", text(x1), "--to-y", text(y1), "--floor", floor});
    REQUIRE(second.exitCode == kExitOk);
    const std::string b = second.answer.at("result").at("to");
    const auto third = sample.draw({"--from", b, "--to-x", text(x0), "--to-y", text(y1), "--floor", floor});
    REQUIRE(third.exitCode == kExitOk);
    const std::string c = third.answer.at("result").at("to");
    REQUIRE(sample.draw({"--from", c, "--to", start, "--floor", floor}).exitCode == kExitOk);
    REQUIRE(sample.run({"room", "add", "--x", text((x0 + x1) / 2.0), "--y", text((y0 + y1) / 2.0), "--floor", floor})
                .exitCode == kExitOk);
}

}

TEST_CASE("canopy add makes a flat slab canopy with the defaults and saves it", "[cli][canopy]") {
    Sample sample("canopy-add");
    sample.drawRoomSquare();
    const auto added = addPorch(sample);
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("command") == "canopy add");
    const auto& canopy = added.answer.at("result");
    CHECK(canopy.at("id") == "cn1");
    CHECK(canopy.at("floor") == "f1");
    REQUIRE(canopy.at("outline").size() == 4);
    CHECK(canopy.at("outline").at(1) == nlohmann::json::array({8.0, 3.0}));
    CHECK_THAT(canopy.at("area").get<double>(), WithinAbs(12.0, 1e-9));
    CHECK_THAT(canopy.at("height").get<double>(), WithinAbs(2.7, 1e-9));
    CHECK_THAT(canopy.at("thickness").get<double>(), WithinAbs(0.25, 1e-9));
    CHECK(canopy.at("roof").at("type") == "flat");
    CHECK(canopy.at("roof").at("overhang") == 0.0);
    CHECK(canopy.at("roof").at("parapet") == 0.0);
    CHECK(canopy.at("roof").at("direction").is_null());
    CHECK(canopy.at("material").is_null());
    CHECK(canopy.at("underside_material").is_null());
    CHECK(canopy.at("fascia_material").is_null());
    const auto codes = codesOf(canopy.at("problems"));
    CHECK_FALSE(holds(codes, "canopy_unsupported"));
    CHECK_FALSE(holds(codes, "canopy_outline_invalid"));

    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    const auto& saved = plan.at("floors").at(0).at("canopies");
    REQUIRE(saved.size() == 1);
    CHECK(saved.at(0).at("id") == "cn1");
    CHECK(saved.at(0).at("outline").at(2).at("x") == 8.0);
    CHECK(saved.at(0).at("outline").at(2).at("y") == 7.0);
}

TEST_CASE("canopy add takes a roof type, its settings and points in one quoted value", "[cli][canopy]") {
    Sample sample("canopy-add-gable");
    sample.drawRoomSquare();
    const auto added = sample.run({"canopy", "add", "--points", "5,3;8,3;8,7;5,7", "--type", "gable", "--pitch", "25",
                                   "--direction", "90", "--overhang", "0.2", "--height", "2.4", "--thickness", "0.2"});
    REQUIRE(added.exitCode == kExitOk);
    const auto& canopy = added.answer.at("result");
    CHECK(canopy.at("outline").size() == 4);
    CHECK(canopy.at("roof").at("type") == "gable");
    CHECK(canopy.at("roof").at("pitch") == 25.0);
    CHECK(canopy.at("roof").at("direction") == 90.0);
    CHECK_THAT(canopy.at("roof").at("overhang").get<double>(), WithinAbs(0.2, 1e-9));
    CHECK_THAT(canopy.at("height").get<double>(), WithinAbs(2.4, 1e-9));
    CHECK_THAT(canopy.at("thickness").get<double>(), WithinAbs(0.2, 1e-9));

    const auto spaced = sample.run({"canopy", "add", "--points", "10,10 12,10 12,12", "--floor", "f1"});
    REQUIRE(spaced.exitCode == kExitOk);
    CHECK(spaced.answer.at("result").at("id") == "cn2");
    CHECK(spaced.answer.at("result").at("outline").size() == 3);
}

TEST_CASE("canopy add refuses bad outlines, values, floors and options and changes nothing", "[cli][canopy]") {
    Sample sample("canopy-add-refuse");
    sample.drawRoomSquare();
    const std::string before = readFile(sample.folder / "plan.json");

    const auto two = sample.run({"canopy", "add", "--points", "5,3", "8,3"});
    CHECK(two.exitCode == kExitFailed);
    CHECK(errorCode(two) == "invalid_value");
    const auto crossing = sample.run({"canopy", "add", "--points", "5,3", "8,7", "8,3", "5,7"});
    CHECK(errorCode(crossing) == "invalid_value");
    CHECK(errorCode(addPorch(sample, {"--height", "0.5"})) == "invalid_value");
    CHECK(errorCode(addPorch(sample, {"--thickness", "2"})) == "invalid_value");
    CHECK(errorCode(addPorch(sample, {"--type", "teapot"})) == "invalid_value");
    CHECK(errorCode(addPorch(sample, {"--type", "gable", "--pitch", "2"})) == "invalid_value");
    CHECK(errorCode(addPorch(sample, {"--overhang", "3"})) == "invalid_value");
    CHECK(errorCode(addPorch(sample, {"--direction", "360"})) == "invalid_value");
    const auto floor = addPorch(sample, {"--floor", "f9"});
    CHECK(floor.exitCode == kExitFailed);
    CHECK(errorCode(floor) == "not_found");

    const auto missing = sample.run({"canopy", "add", "--height", "2.5"});
    CHECK(missing.exitCode == kExitBadArguments);
    CHECK(errorCode(missing) == "bad_arguments");
    const auto word = sample.run({"canopy", "add", "--points", "5,3", "eight,3", "8,7"});
    CHECK(word.exitCode == kExitBadArguments);
    CHECK(errorCode(word) == "bad_arguments");
    const auto lone = sample.run({"canopy", "add", "--points", "5", "8,3", "8,7"});
    CHECK(errorCode(lone) == "bad_arguments");
    const auto direction = addPorch(sample, {"--direction", "north"});
    CHECK(direction.exitCode == kExitBadArguments);
    CHECK(errorCode(direction) == "bad_arguments");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("a canopy with nothing under it is reported in its entry and by build --check", "[cli][canopy]") {
    Sample sample("canopy-unsupported");
    sample.drawRoomSquare();
    const auto added = sample.run({"canopy", "add", "--points", "10,10", "12,10", "12,12", "10,12"});
    REQUIRE(added.exitCode == kExitOk);
    const auto& problems = added.answer.at("result").at("problems");
    REQUIRE(holds(codesOf(problems), "canopy_unsupported"));
    for (const auto& problem : problems) {
        if (problem.at("code") == "canopy_unsupported") {
            CHECK(problem.at("severity") == "warning");
            CHECK(problem.at("message").get<std::string>().find("cn1") != std::string::npos);
        }
    }

    const auto check = sample.run({"build", "--check"});
    REQUIRE(check.exitCode == kExitOk);
    bool found = false;
    for (const auto& problem : check.answer.at("result").at("problems")) {
        if (problem.at("code") == "canopy_unsupported") {
            found = true;
            CHECK(problem.at("severity") == "warning");
            CHECK(problem.at("floor") == "f1");
            CHECK(problem.at("object") == "cn1");
        }
    }
    CHECK(found);

    REQUIRE(sample.run({"canopy", "set", "cn1", "--points", "5,3", "8,3", "8,7", "5,7"}).exitCode == kExitOk);
    const auto after = sample.run({"build", "--check"});
    REQUIRE(after.exitCode == kExitOk);
    CHECK_FALSE(holds(codesOf(after.answer.at("result").at("problems")), "canopy_unsupported"));
}

TEST_CASE("a canopy wholly inside the building is reported as canopy_outline_invalid", "[cli][canopy]") {
    Sample sample("canopy-inside");
    sample.drawRoomSquare();
    const auto added = sample.run({"canopy", "add", "--points", "2,4", "4,4", "4,6", "2,6"});
    REQUIRE(added.exitCode == kExitOk);
    const auto codes = codesOf(added.answer.at("result").at("problems"));
    CHECK(holds(codes, "canopy_outline_invalid"));
    const auto check = sample.run({"build", "--check"});
    REQUIRE(check.exitCode == kExitOk);
    CHECK(holds(codesOf(check.answer.at("result").at("problems")), "canopy_outline_invalid"));
    CHECK(check.answer.at("result").at("error_count").get<int>() >= 1);
}

TEST_CASE("an upper floor that overhangs too far is reported by build --check", "[cli][canopy][overhang]") {
    Sample sample("canopy-overhang");
    sample.drawRoomSquare();
    const auto floor = sample.run({"floor", "add"});
    REQUIRE(floor.exitCode == kExitOk);
    const std::string upper = floor.answer.at("result").at("id");
    drawBox(sample, upper, 1.0, 3.0, 9.5, 7.0);

    const auto check = sample.run({"build", "--check"});
    REQUIRE(check.exitCode == kExitOk);
    bool found = false;
    for (const auto& problem : check.answer.at("result").at("problems")) {
        if (problem.at("code") == "floor_overhang") {
            found = true;
            CHECK(problem.at("severity") == "warning");
            CHECK(problem.at("floor") == upper);
            CHECK(problem.at("object") == upper);
        }
    }
    CHECK(found);
}

TEST_CASE("an upper floor that overhangs a little is not reported", "[cli][canopy][overhang]") {
    Sample sample("canopy-overhang-small");
    sample.drawRoomSquare();
    const auto floor = sample.run({"floor", "add"});
    REQUIRE(floor.exitCode == kExitOk);
    const std::string upper = floor.answer.at("result").at("id");
    drawBox(sample, upper, 1.0, 3.0, 6.5, 7.0);

    const auto check = sample.run({"build", "--check"});
    REQUIRE(check.exitCode == kExitOk);
    CHECK_FALSE(holds(codesOf(check.answer.at("result").at("problems")), "floor_overhang"));
}

TEST_CASE("canopy set changes what is given and keeps the rest", "[cli][canopy]") {
    Sample sample("canopy-set");
    sample.drawRoomSquare();
    REQUIRE(addPorch(sample, {"--height", "2.4"}).exitCode == kExitOk);

    const auto typed = sample.run({"canopy", "set", "cn1", "--type", "shed", "--pitch", "15", "--direction", "0"});
    REQUIRE(typed.exitCode == kExitOk);
    CHECK(typed.answer.at("command") == "canopy set");
    const auto& canopy = typed.answer.at("result");
    CHECK(canopy.at("id") == "cn1");
    CHECK(canopy.at("roof").at("type") == "shed");
    CHECK(canopy.at("roof").at("pitch") == 15.0);
    CHECK(canopy.at("roof").at("direction") == 0.0);
    CHECK_THAT(canopy.at("height").get<double>(), WithinAbs(2.4, 1e-9));
    CHECK(canopy.at("outline").size() == 4);

    const auto automatic = sample.run({"canopy", "set", "cn1", "--direction", "auto", "--thickness", "0.3"});
    REQUIRE(automatic.exitCode == kExitOk);
    CHECK(automatic.answer.at("result").at("roof").at("direction").is_null());
    CHECK(automatic.answer.at("result").at("roof").at("type") == "shed");
    CHECK_THAT(automatic.answer.at("result").at("thickness").get<double>(), WithinAbs(0.3, 1e-9));

    const auto moved = sample.run({"canopy", "set", "cn1", "--points", "5,3", "7,3", "7,5"});
    REQUIRE(moved.exitCode == kExitOk);
    CHECK(moved.answer.at("result").at("outline").size() == 3);
    CHECK_THAT(moved.answer.at("result").at("area").get<double>(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("canopy set refuses an unknown canopy, a bad value and no option, changing nothing", "[cli][canopy]") {
    Sample sample("canopy-set-refuse");
    sample.drawRoomSquare();
    REQUIRE(addPorch(sample).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");

    CHECK(errorCode(sample.run({"canopy", "set", "cn9", "--height", "2.5"})) == "not_found");
    CHECK(errorCode(sample.run({"canopy", "set", "w1", "--height", "2.5"})) == "not_found");
    CHECK(errorCode(sample.run({"canopy", "set", "cn1", "--height", "25"})) == "invalid_value");
    CHECK(errorCode(sample.run({"canopy", "set", "cn1", "--type", "hip", "--pitch", "1"})) == "invalid_value");
    CHECK(errorCode(sample.run({"canopy", "set", "cn1", "--points", "5,3", "8,3"})) == "invalid_value");
    const auto none = sample.run({"canopy", "set", "cn1"});
    CHECK(none.exitCode == kExitBadArguments);
    CHECK(errorCode(none) == "bad_arguments");
    CHECK(errorCode(sample.run({"canopy", "set", "cn1", "--points", "5;3"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"canopy", "set", "cn1", "--direction", "east"})) == "bad_arguments");

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("canopy materials are checked against the library and none clears them", "[cli][canopy]") {
    MaterialLibrary library("canopy-material");
    Sample sample("canopy-material-project");
    sample.drawRoomSquare();
    const auto added = addPorch(sample, {"--material", "grey_tile", "--underside-material", "white_paint",
                                         "--fascia-material", "painted_wood"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("material") == "grey_tile");
    CHECK(added.answer.at("result").at("underside_material") == "white_paint");
    CHECK(added.answer.at("result").at("fascia_material") == "painted_wood");

    const std::string before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"canopy", "set", "cn1", "--fascia-material", "no_such_material"})) == "not_found");
    CHECK(errorCode(addPorch(sample, {"--material", "no_such_material"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto cleared = sample.run({"canopy", "set", "cn1", "--underside-material", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("underside_material").is_null());
    CHECK(cleared.answer.at("result").at("material") == "grey_tile");
}

TEST_CASE("canopy remove and canopy list", "[cli][canopy]") {
    Sample sample("canopy-remove");
    sample.drawRoomSquare();
    REQUIRE(addPorch(sample).exitCode == kExitOk);
    const auto floor = sample.run({"floor", "add"});
    REQUIRE(floor.exitCode == kExitOk);
    const std::string upper = floor.answer.at("result").at("id");
    REQUIRE(sample.run({"canopy", "add", "--points", "1,1", "3,1", "3,2", "--floor", upper, "--height", "1.5"}).exitCode ==
            kExitOk);

    const auto listed = sample.run({"canopy", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    const auto& canopies = listed.answer.at("result").at("canopies");
    REQUIRE(canopies.size() == 2);
    CHECK(canopies.at(0).at("id") == "cn1");
    CHECK(canopies.at(0).at("floor") == "f1");
    CHECK(canopies.at(1).at("floor") == upper);
    CHECK(canopies.at(1).at("problems").is_array());
    const auto one = sample.run({"canopy", "list", "--floor", upper});
    REQUIRE(one.answer.at("result").at("canopies").size() == 1);
    CHECK(errorCode(sample.run({"canopy", "list", "--floor", "f9"})) == "not_found");

    const auto removed = sample.run({"canopy", "remove", "cn1"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("command") == "canopy remove");
    CHECK(removed.answer.at("result").at("removed") == "cn1");
    const auto after = sample.run({"canopy", "list"});
    REQUIRE(after.answer.at("result").at("canopies").size() == 1);
    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    CHECK(plan.at("floors").at(0).at("canopies").empty());

    const std::string before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"canopy", "remove", "cn1"})) == "not_found");
    CHECK(errorCode(sample.run({"canopy", "remove", "w1"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("the remove commands of other kinds point a canopy id to canopy remove", "[cli][canopy]") {
    Sample sample("canopy-other-remove");
    sample.drawRoomSquare();
    REQUIRE(addPorch(sample).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");
    for (const char* word : {"item", "opening"}) {
        const auto refused = sample.run({word, "remove", "cn1"});
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "invalid_value");
        CHECK(refused.answer.at("error").at("message").get<std::string>().find("canopy remove") != std::string::npos);
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("canopy edits follow the lock rules and canopy list never is refused", "[cli][canopy]") {
    Sample sample("canopy-lock");
    sample.drawRoomSquare();
    REQUIRE(addPorch(sample).exitCode == kExitOk);
    appOpens(sample.folder);
    const std::string before = readFile(sample.folder / "plan.json");

    for (const auto& args : std::vector<std::vector<std::string>>{
             {"canopy", "add", "--points", "10,10", "12,10", "12,12"},
             {"canopy", "set", "cn1", "--height", "2.5"},
             {"canopy", "remove", "cn1"}}) {
        const auto refused = sample.run(args);
        CHECK(refused.exitCode == kExitFailed);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(sample.run({"canopy", "list"}).exitCode == kExitOk);

    REQUIRE(sample.run({"session", "begin"}).exitCode == kExitOk);
    CHECK(sample.run({"canopy", "set", "cn1", "--height", "2.5"}).exitCode == kExitOk);
    REQUIRE(sample.run({"session", "end"}).exitCode == kExitOk);
}

TEST_CASE("help lists the canopy commands", "[cli][canopy]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"canopy add", "canopy set", "canopy remove", "canopy list"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
    CHECK(plynth::cli::isCommandWord("canopy"));
}
