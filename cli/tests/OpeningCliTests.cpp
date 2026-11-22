#include "SampleProject.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth::cli;
using namespace plynth::cli::testing;
using Catch::Matchers::WithinAbs;

namespace {

// Writes a text file, making its folder first.
void writeFile(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

// A version 1 project folder with one door on a wall, as milestone 4 wrote it.
std::filesystem::path makeVersionOneProject(const TempFolder& temp) {
    const auto folder = temp.path() / "old";
    writeFile(folder / "project.json",
              R"({ "format": "plynth-project", "version": 1, "name": "Old", "export_mode": "single", "grid_size": 0.5 })");
    writeFile(folder / "plan.json", R"({
  "format": "plynth-plan", "version": 1, "scale": { "pixels_per_metre": null },
  "floors": [ {
    "id": "f1", "name": "Floor 1", "image": null, "ceiling_height": 3.0,
    "points": [ { "id": "p1", "x": 0.0, "y": 0.0, "corner_radius": 0.0 }, { "id": "p2", "x": 4.0, "y": 0.0, "corner_radius": 0.0 } ],
    "walls": [ { "id": "w1", "from": "p1", "to": "p2", "thickness": 0.2, "height": null, "bulge": 0.0, "material_inside": null, "material_outside": null } ],
    "rooms": [],
    "openings": [ { "id": "o1", "wall": "w1", "offset": 2.0, "type": "door", "shape": "rectangle", "width": 0.9, "height": 2.1, "sill": 0.0, "frame": null } ]
  } ]
})");
    return folder;
}

}

TEST_CASE("opening shapes lists the five built-in shapes without a project", "[cli][opening]") {
    TempFolder temp("shapes");
    const auto outcome = runChecked({"opening", "shapes"}, temp.path());
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "opening shapes");
    const auto& shapes = outcome.answer.at("result").at("shapes");
    std::vector<std::string> names;
    for (const auto& shape : shapes) {
        if (!shape.at("built_in").get<bool>()) {
            continue; // a custom shape another test registered in this process
        }
        names.push_back(shape.at("id").get<std::string>());
        CHECK(shape.at("built_in") == true);
        CHECK(shape.at("name").is_string());
        CHECK(shape.at("size_rule").is_string());
    }
    CHECK(names == std::vector<std::string>{"rectangle", "round_arch", "flat_arch", "pointed_arch", "circle"});
    CHECK(shapes.at(0).at("name") == "Rectangle");
    CHECK(shapes.at(0).at("size_rule") == "");
    CHECK(shapes.at(1).at("size_rule") != "");
}

TEST_CASE("floor list gives one floor with a ceiling height of 3", "[cli][floor]") {
    const Sample sample("floor-list");
    const auto outcome = sample.run({"floor", "list"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& floors = outcome.answer.at("result").at("floors");
    REQUIRE(floors.size() == 1);
    CHECK(floors.at(0).at("id") == "f1");
    CHECK(floors.at(0).at("name") == "Floor 1");
    CHECK(floors.at(0).at("image") == "plans/apartment-01.png");
    CHECK_THAT(floors.at(0).at("ceiling_height").get<double>(), WithinAbs(3.0, 1e-12));
}

TEST_CASE("project info reports the default sizes", "[cli][project]") {
    const Sample sample("defaults-info");
    const auto outcome = sample.run({"project", "info"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& defaults = outcome.answer.at("result").at("defaults");
    CHECK(defaults.at("ceiling_height") == 3.0);
    CHECK(defaults.at("door_width") == 0.9);
    CHECK(defaults.at("door_height") == 2.1);
    CHECK(defaults.at("window_width") == 1.2);
    CHECK(defaults.at("window_height") == 1.2);
    CHECK(defaults.at("window_sill") == 0.9);
}

TEST_CASE("project set changes the default sizes and saves version 3", "[cli][project]") {
    const Sample sample("defaults-set");
    const auto set = sample.run({"project", "set", "--door-height", "2.0"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("defaults").at("door_height") == 2.0);
    const auto info = sample.run({"project", "info"});
    CHECK(info.answer.at("result").at("defaults").at("door_height") == 2.0);
    CHECK(info.answer.at("result").at("defaults").at("door_width") == 0.9);
    const auto file = nlohmann::json::parse(readFile(sample.folder / "project.json"));
    CHECK(file.at("version") == 4);
    CHECK(file.at("defaults").at("door_height") == 2.0);
}

TEST_CASE("project set takes the grid and the defaults together", "[cli][project]") {
    const Sample sample("defaults-both");
    const auto set =
        sample.run({"project", "set", "--grid", "0.25", "--new-floor-height", "2.8", "--window-sill", "1.0"});
    REQUIRE(set.exitCode == kExitOk);
    const auto& result = set.answer.at("result");
    CHECK(result.at("grid_size") == 0.25);
    CHECK(result.at("defaults").at("ceiling_height") == 2.8);
    CHECK(result.at("defaults").at("window_sill") == 1.0);
}

TEST_CASE("project set refuses a window sill that does not fit and leaves the file unchanged",
          "[cli][project]") {
    const Sample sample("defaults-refuse");
    const auto before = readFile(sample.folder / "project.json");
    const auto outcome = sample.run({"project", "set", "--window-sill", "2.5"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(readFile(sample.folder / "project.json") == before);
}

TEST_CASE("project set with a refused second value changes nothing", "[cli][project]") {
    const Sample sample("defaults-group");
    const auto before = readFile(sample.folder / "project.json");
    const auto outcome = sample.run({"project", "set", "--grid", "0.25", "--door-height", "9"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(readFile(sample.folder / "project.json") == before);
}

TEST_CASE("project set without an option is bad_arguments", "[cli][project]") {
    const Sample sample("defaults-none");
    const auto outcome = sample.run({"project", "set"});
    CHECK(outcome.exitCode == kExitBadArguments);
    CHECK(errorCode(outcome) == "bad_arguments");
}

TEST_CASE("a version 1 project reads and the first edit saves version 3", "[cli][project]") {
    const TempFolder temp("v1");
    const auto folder = makeVersionOneProject(temp);
    const std::string project = utf8(folder);
    const std::string planBefore = readFile(folder / "plan.json");

    const auto info = runChecked({"project", "info", "--project", project});
    REQUIRE(info.exitCode == kExitOk);
    CHECK(info.answer.at("result").at("defaults").at("door_height") == 2.1);
    CHECK(info.answer.at("result").at("plan").at("version") == 6);
    const auto points = runChecked({"point", "list", "--project", project});
    REQUIRE(points.exitCode == kExitOk);
    CHECK(points.answer.at("result").at("points").size() == 2);
    CHECK(readFile(folder / "plan.json") == planBefore);

    const auto edit = runChecked({"point", "add", "--x", "1", "--y", "1", "--project", project});
    REQUIRE(edit.exitCode == kExitOk);
    const auto plan = nlohmann::json::parse(readFile(folder / "plan.json"));
    CHECK(plan.at("version") == 6);
    const auto& opening = plan.at("floors").at(0).at("openings").at(0);
    CHECK(opening.at("hinge") == "start");
    CHECK(opening.at("swing") == "right");
    const auto projectFile = nlohmann::json::parse(readFile(folder / "project.json"));
    CHECK(projectFile.at("version") == 4);
    CHECK(projectFile.contains("defaults"));
}

TEST_CASE("project set changes the wall item centre and refuses one above the ceiling", "[cli][project]") {
    const Sample sample("wall-item-centre");
    const auto info = sample.run({"project", "info"});
    CHECK(info.answer.at("result").at("defaults").at("wall_item_centre") == 1.2);

    const auto set = sample.run({"project", "set", "--wall-item-centre", "1.4"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("defaults").at("wall_item_centre") == 1.4);
    const auto again = sample.run({"project", "info"});
    CHECK(again.answer.at("result").at("defaults").at("wall_item_centre") == 1.4);

    const auto before = readFile(sample.folder / "project.json");
    const auto refused = sample.run({"project", "set", "--wall-item-centre", "3.5"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    CHECK(readFile(sample.folder / "project.json") == before);
}

TEST_CASE("a version 2 project is saved as version 3 by an edit command", "[cli][project]") {
    const TempFolder temp("v2");
    const auto folder = temp.path() / "old2";
    writeFile(folder / "project.json", R"({ "format": "plynth-project", "version": 2, "name": "Old", "export_mode": "single", "grid_size": 0.5,
  "defaults": { "ceiling_height": 3.0, "door_width": 0.9, "door_height": 2.1, "window_width": 1.2, "window_height": 1.2, "window_sill": 0.9 } })");
    writeFile(folder / "plan.json", R"({
  "format": "plynth-plan", "version": 2, "scale": { "pixels_per_metre": null },
  "floors": [ { "id": "f1", "name": "Floor 1", "image": null, "ceiling_height": 3.0,
    "points": [], "walls": [], "rooms": [], "openings": [] } ]
})");
    const std::string project = utf8(folder);
    const auto edit = runChecked({"point", "add", "--x", "1", "--y", "1", "--project", project});
    REQUIRE(edit.exitCode == kExitOk);
    const auto plan = nlohmann::json::parse(readFile(folder / "plan.json"));
    CHECK(plan.at("version") == 6);
    CHECK(plan.at("floors").at(0).at("items").is_array());
    CHECK(plan.at("floors").at(0).at("items").empty());
    const auto projectFile = nlohmann::json::parse(readFile(folder / "project.json"));
    CHECK(projectFile.at("version") == 4);
    CHECK(projectFile.at("defaults").at("wall_item_centre") == 1.2);
}

TEST_CASE("help lists the new commands", "[cli]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"opening shapes", "floor list", "project set"}) {
        CHECK(std::find(names.begin(), names.end(), wanted) != names.end());
    }
    CHECK(isCommandWord("opening"));
    CHECK(isCommandWord("floor"));
}
