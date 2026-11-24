#include "SampleProject.h"

#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;

namespace {

// Reads a JSON file.
nlohmann::json readJson(const std::filesystem::path& file) {
    return nlohmann::json::parse(readFile(file));
}

// Writes a JSON file in full.
void writeJson(const std::filesystem::path& file, const nlohmann::json& json) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << json.dump(2);
}

// Gives a sample a closed room and a door, then rewrites its files as plan format 6 and project format 5: no
// lights, no set dressing switch, no frame picking, no trim, no variation.
void makeOlder(const Sample& sample) {
    sample.drawRoomSquare();
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--type", "door"}).exitCode == kExitOk);
    nlohmann::json plan = readJson(sample.folder / "plan.json");
    plan["version"] = 6;
    for (auto& floor : plan.at("floors")) {
        floor.erase("lights");
        for (auto& room : floor.at("rooms")) {
            room.erase("dressing");
        }
        for (auto& opening : floor.at("openings")) {
            opening.erase("auto_frame");
        }
    }
    writeJson(sample.folder / "plan.json", plan);
    nlohmann::json project = readJson(sample.folder / "project.json");
    project["version"] = 5;
    project.erase("trim");
    project.erase("variation");
    if (project.contains("materials")) {
        project.at("materials").erase("trim");
        project.at("materials").erase("railing");
    }
    writeJson(sample.folder / "project.json", project);
}

}

TEST_CASE("version answers with plan format 7 and project format 6", "[cli][format]") {
    const auto outcome = plynth::cli::testing::runChecked({"version"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("plan_format") == 7);
    CHECK(outcome.answer.at("result").at("project_format") == 6);
    CHECK(plynth::kPlanFormatVersion == 7);
    CHECK(plynth::kProjectFormatVersion == 6);
}

TEST_CASE("a new project is written as plan 7 and project 6 with the new fields", "[cli][format]") {
    Sample sample("format-new");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2"}).exitCode == kExitOk);
    const auto plan = readJson(sample.folder / "plan.json");
    CHECK(plan.at("version") == 7);
    const auto& floor = plan.at("floors").at(0);
    CHECK(floor.at("lights").is_array());
    CHECK(floor.at("rooms").at(0).at("dressing") == true);
    CHECK(floor.at("openings").at(0).at("auto_frame") == true);
    const auto project = readJson(sample.folder / "project.json");
    CHECK(project.at("version") == 6);
    CHECK(project.at("trim").at("skirting").at("profile") == "classic");
    CHECK(project.at("variation").at("seed") == 0);
}

TEST_CASE("plan 6 and project 5 files read with the defaults and are not written by reading", "[cli][format]") {
    const Sample sample("format-old-read");
    makeOlder(sample);
    const std::string planBefore = readFile(sample.folder / "plan.json");
    const std::string projectBefore = readFile(sample.folder / "project.json");

    const auto info = sample.run({"project", "info"});
    REQUIRE(info.exitCode == kExitOk);
    CHECK(info.answer.at("result").at("plan").at("version") == 7);
    CHECK(info.answer.at("result").at("plan").at("floors").at(0).at("rooms") == 1);

    const auto lights = sample.run({"light", "list"});
    REQUIRE(lights.exitCode == kExitOk);
    CHECK(lights.answer.at("result").at("lights").empty());
    const auto rooms = sample.run({"room", "list"});
    REQUIRE(rooms.exitCode == kExitOk);
    CHECK(rooms.answer.at("result").at("rooms").at(0).at("dressing") == true);
    CHECK(rooms.answer.at("result").at("rooms").at(0).at("type") == "living");
    const auto openings = sample.run({"opening", "list"});
    REQUIRE(openings.exitCode == kExitOk);
    CHECK(openings.answer.at("result").at("openings").at(0).at("auto_frame") == true);
    CHECK(openings.answer.at("result").at("openings").at(0).at("frame").is_null());
    const auto trim = sample.run({"trim", "show"});
    REQUIRE(trim.exitCode == kExitOk);
    CHECK(trim.answer.at("result").at("skirting").at("enabled") == true);
    CHECK(trim.answer.at("result").at("railing_height") == 0.9);
    const auto variation = sample.run({"variation", "show"});
    REQUIRE(variation.exitCode == kExitOk);
    CHECK(variation.answer.at("result").at("seed") == 0);

    CHECK(readFile(sample.folder / "plan.json") == planBefore);
    CHECK(readFile(sample.folder / "project.json") == projectBefore);
}

TEST_CASE("the first edit of plan 6 and project 5 files saves plan 7 and project 6", "[cli][format]") {
    const Sample sample("format-old-edit");
    makeOlder(sample);
    REQUIRE(sample.run({"point", "add", "--x", "8", "--y", "8"}).exitCode == kExitOk);
    const auto plan = readJson(sample.folder / "plan.json");
    CHECK(plan.at("version") == 7);
    const auto& floor = plan.at("floors").at(0);
    CHECK(floor.at("lights").empty());
    CHECK(floor.at("rooms").at(0).at("dressing") == true);
    CHECK(floor.at("openings").at(0).at("auto_frame") == true);
    const auto project = readJson(sample.folder / "project.json");
    CHECK(project.at("version") == 6);
    CHECK(project.contains("trim"));
    CHECK(project.contains("variation"));
}

TEST_CASE("a plan of version 8 is refused with unsupported_version and its reason", "[cli][format]") {
    Sample sample("format-newer-plan");
    nlohmann::json plan = readJson(sample.folder / "plan.json");
    plan["version"] = 8;
    writeJson(sample.folder / "plan.json", plan);
    const std::string before = readFile(sample.folder / "plan.json");

    for (const auto& args : std::vector<std::vector<std::string>>{
             {"project", "info"}, {"light", "list"}, {"trim", "show"}, {"variation", "show"},
             {"light", "add", "--x", "1", "--y", "1"}, {"trim", "set", "skirting", "--off"}}) {
        const auto outcome = sample.run(args);
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "unsupported_version");
        CHECK_FALSE(outcome.answer.at("error").at("message").get<std::string>().empty());
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("a project file of version 7 is refused with unsupported_version", "[cli][format]") {
    Sample sample("format-newer-project");
    nlohmann::json project = readJson(sample.folder / "project.json");
    project["version"] = 7;
    writeJson(sample.folder / "project.json", project);
    const std::string before = readFile(sample.folder / "project.json");

    const auto outcome = sample.run({"variation", "set", "--seed", "3"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "unsupported_version");
    CHECK_FALSE(outcome.answer.at("error").at("message").get<std::string>().empty());
    CHECK(readFile(sample.folder / "project.json") == before);
}
