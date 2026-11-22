#include "SampleProject.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::ids;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;

namespace {

// The floor entry with an id in a floor list answer.
nlohmann::json floorOf(const Sample& sample, const std::string& id) {
    const auto list = sample.run({"floor", "list"});
    for (const auto& entry : list.answer.at("result").at("floors")) {
        if (entry.at("id") == id) {
            return entry;
        }
    }
    return {};
}

}

TEST_CASE("floor add puts a floor on top with its elevation", "[cli][floor]") {
    Sample sample("floor-add");
    const auto added = sample.run({"floor", "add", "--name", "Upper"});
    REQUIRE(added.exitCode == kExitOk);
    const auto& entry = added.answer.at("result");
    CHECK(entry.at("id") == "f2");
    CHECK(entry.at("name") == "Upper");
    CHECK(entry.at("image").is_null());
    CHECK(entry.at("slab_thickness") == 0.2);
    CHECK(entry.at("image_offset").at("x") == 0.0);
    CHECK_THAT(entry.at("elevation").get<double>(), WithinAbs(3.2, 1e-9));
    CHECK_THAT(floorOf(sample, "f1").at("rise").get<double>(), WithinAbs(3.2, 1e-9));
    CHECK(floorOf(sample, "f1").at("elevation") == 0.0);

    const auto list = sample.run({"floor", "list"}).answer.at("result").at("floors");
    CHECK(list.size() == 2);
    CHECK(list.at(1).at("id") == "f2");
    CHECK(list.at(1).at("stairs").empty());
}

TEST_CASE("floor add takes heights, a default name and --below", "[cli][floor]") {
    Sample sample("floor-below");
    const auto below =
        sample.run({"floor", "add", "--below", "f1", "--ceiling-height", "2.5", "--slab-thickness", "0.3"});
    REQUIRE(below.exitCode == kExitOk);
    CHECK(below.answer.at("result").at("name") == "Floor 2");
    CHECK(below.answer.at("result").at("ceiling_height") == 2.5);
    CHECK(below.answer.at("result").at("slab_thickness") == 0.3);
    CHECK(below.answer.at("result").at("elevation") == 0.0);
    const auto list = sample.run({"floor", "list"}).answer.at("result").at("floors");
    CHECK(list.at(0).at("id") == "f2");
    CHECK(list.at(1).at("id") == "f1");
    CHECK_THAT(list.at(1).at("elevation").get<double>(), WithinAbs(2.8, 1e-9));
}

TEST_CASE("floor add refuses bad values and leaves the plan alone", "[cli][floor]") {
    Sample sample("floor-add-bad");
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"floor", "add", "--below", "f9"})) == "not_found");
    CHECK(errorCode(sample.run({"floor", "add", "--slab-thickness", "0.01"})) == "invalid_value");
    CHECK(errorCode(sample.run({"floor", "add", "--ceiling-height", "0"})) == "invalid_value");
    CHECK(errorCode(sample.run({"floor", "add", "--image", "missing.png"})) == "image_error");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("floor add and set take a plan image that is copied into the project", "[cli][floor]") {
    Sample sample("floor-image");
    const auto added = sample.run({"floor", "add", "--image", PLYNTH_SAMPLE_PLAN});
    REQUIRE(added.exitCode == kExitOk);
    const std::string stored = added.answer.at("result").at("image").get<std::string>();
    CHECK(stored.starts_with("plans/"));
    CHECK(std::filesystem::exists(sample.folder / stored));

    const auto cleared = sample.run({"floor", "set", "--floor", "f2", "--image", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("image").is_null());
    const auto set = sample.run({"floor", "set", "--floor", "f2", "--image", PLYNTH_SAMPLE_PLAN});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("image").get<std::string>().starts_with("plans/"));
}

TEST_CASE("floor set changes names and heights as one step", "[cli][floor]") {
    Sample sample("floor-set");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    const auto set = sample.run({"floor", "set", "--floor", "f2", "--name", "Attic", "--ceiling-height", "2.4",
                                 "--slab-thickness", "0.25"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("name") == "Attic");
    CHECK(set.answer.at("result").at("ceiling_height") == 2.4);
    CHECK(set.answer.at("result").at("slab_thickness") == 0.25);

    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"floor", "set", "--floor", "f2", "--name", "Ok", "--slab-thickness", "5"})) ==
          "invalid_value");
    CHECK(errorCode(sample.run({"floor", "set", "--floor", "f9", "--name", "x"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
    CHECK(sample.run({"floor", "set"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"floor", "set", "--image-offset", "1"}).exitCode == kExitBadArguments);
}

TEST_CASE("floor set --image-offset moves the marks with the image unless --keep-marks", "[cli][floor]") {
    Sample sample("floor-offset");
    REQUIRE(sample.run({"scale", "set", "--pixels-per-metre", "100"}).exitCode == kExitOk);
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"point", "add", "--floor", "f2", "--x", "2", "--y", "2"}).exitCode == kExitOk);
    REQUIRE(sample.run({"point", "add", "--x", "2", "--y", "2"}).exitCode == kExitOk);

    const auto moved = sample.run({"floor", "set", "--floor", "f2", "--image-offset", "1,0"});
    REQUIRE(moved.exitCode == kExitOk);
    CHECK(moved.answer.at("result").at("image_offset").at("x") == 1.0);
    const auto points = sample.run({"point", "list", "--floor", "f2"}).answer.at("result").at("points");
    CHECK(points.at(0).at("x") == 3.0);
    CHECK(points.at(0).at("y") == 2.0);

    const auto kept = sample.run({"floor", "set", "--floor", "f2", "--image-offset", "2,0", "--keep-marks"});
    REQUIRE(kept.exitCode == kExitOk);
    CHECK(sample.run({"point", "list", "--floor", "f2"}).answer.at("result").at("points").at(0).at("x") == 3.0);
    CHECK(sample.run({"point", "list", "--floor", "f1"}).answer.at("result").at("points").at(0).at("x") == 2.0);
}

TEST_CASE("floor remove needs --yes and refuses the only floor", "[cli][floor]") {
    Sample sample("floor-remove");
    const auto only = sample.run({"floor", "remove", "f1", "--yes"});
    CHECK(only.exitCode == kExitFailed);
    CHECK(errorCode(only) == "invalid_value");

    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(sample.run({"floor", "remove", "f2"}).exitCode == kExitBadArguments);
    CHECK(errorCode(sample.run({"floor", "remove", "f9", "--yes"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto removed = sample.run({"floor", "remove", "f2", "--yes"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == "f2");
    CHECK(removed.answer.at("result").at("stairs_without_floor").empty());
    CHECK(sample.run({"floor", "list"}).answer.at("result").at("floors").size() == 1);
}

TEST_CASE("floor remove lists the stairs left without a floor above", "[cli][floor]") {
    Sample sample("floor-remove-stair");
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    REQUIRE(sample.run({"stair", "add", "--floor", "f1", "--x", "1", "--y", "1"}).exitCode == kExitOk);
    const auto removed = sample.run({"floor", "remove", "f2", "--yes"});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(ids(removed.answer.at("result").at("stairs_without_floor")) == std::vector<std::string>{"s1"});
    const auto list = sample.run({"stair", "list"});
    CHECK(list.answer.at("result").at("stairs").at(0).at("problems").at(0).at("code") == "stair_no_floor_above");
}

TEST_CASE("floor commands are refused while the app has the project open", "[cli][floor]") {
    Sample sample("floor-locked");
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"floor", "add"})) == "locked");
    CHECK(errorCode(sample.run({"floor", "set", "--name", "x"})) == "locked");
    CHECK(errorCode(sample.run({"floor", "remove", "f1", "--yes"})) == "locked");
    CHECK(sample.run({"floor", "list"}).exitCode == kExitOk);
    CHECK(readFile(sample.folder / "plan.json") == before);
}
