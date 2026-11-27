#include "SampleProject.h"

#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>
#include <vector>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::ids;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// Reads a JSON file.
nlohmann::json readJson(const std::filesystem::path& file) {
    return nlohmann::json::parse(readFile(file));
}

// Writes a JSON file in full.
void writeJson(const std::filesystem::path& file, const nlohmann::json& json) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << json.dump(2);
}

// Removes the given keys from every object in a list of an object, when the list is there.
void eraseFromEach(nlohmann::json& owner, const char* list, std::initializer_list<const char*> keys) {
    if (!owner.contains(list)) {
        return;
    }
    for (auto& entry : owner.at(list)) {
        for (const char* key : keys) {
            entry.erase(key);
        }
    }
}

// Removes the plan format 8 fields of one set of roof settings.
void eraseRoofFields8(nlohmann::json& settings) {
    for (const char* key : {"second_pitch", "break_ratio", "ridge_offset", "rise"}) {
        settings.erase(key);
    }
}

// Gives a sample a closed room and a door, then rewrites its files as plan format 6 and project format 5: no
// lights, no set dressing switch, no frame picking, no trim, no variation, and none of the plan format 8 and
// project format 7 fields.
void makeOlder(const Sample& sample) {
    sample.drawRoomSquare();
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--type", "door"}).exitCode == kExitOk);
    nlohmann::json plan = readJson(sample.folder / "plan.json");
    plan["version"] = 6;
    for (const char* key : {"ground_floor", "site", "foundation"}) {
        plan.erase(key);
    }
    if (plan.contains("roof")) {
        eraseRoofFields8(plan.at("roof"));
    }
    if (plan.contains("roof_sections")) {
        for (auto& section : plan.at("roof_sections")) {
            if (section.contains("settings")) {
                eraseRoofFields8(section.at("settings"));
            }
        }
    }
    for (auto& floor : plan.at("floors")) {
        floor.erase("lights");
        for (const char* key : {"decks", "railings", "canopies", "dormers", "light_strips"}) {
            floor.erase(key);
        }
        for (auto& room : floor.at("rooms")) {
            room.erase("dressing");
        }
        for (auto& opening : floor.at("openings")) {
            opening.erase("auto_frame");
        }
        eraseFromEach(floor, "walls", {"cladding"});
        eraseFromEach(floor, "openings", {"fill"});
        eraseFromEach(floor, "items", {"on"});
        eraseFromEach(floor, "stairs", {"rise", "cheeks", "railing"});
    }
    writeJson(sample.folder / "plan.json", plan);
    nlohmann::json project = readJson(sample.folder / "project.json");
    project["version"] = 5;
    project.erase("trim");
    project.erase("variation");
    if (project.contains("materials")) {
        for (const char* key : {"trim", "railing", "deck", "foundation", "ground", "paving", "fence", "boundary_wall"}) {
            project.at("materials").erase(key);
        }
    }
    writeJson(sample.folder / "project.json", project);
}

// The folder that keeps a saved file of every format version and the samples as 0.1.0 and 0.1.1 shipped them.
std::filesystem::path formatsDir() {
    return std::filesystem::path(PLYNTH_FORMATS_DIR);
}

// Copies one saved file of the formats folder to a file, creating its folder.
void copyFormatFile(const std::string& name, const std::filesystem::path& target) {
    std::filesystem::create_directories(target.parent_path());
    std::filesystem::copy_file(formatsDir() / name, target, std::filesystem::copy_options::overwrite_existing);
}

// A project folder with the version 7 plan and the version 6 project file.
std::filesystem::path makeVersionSevenProject(const TempFolder& temp, const std::string& name) {
    const std::filesystem::path folder = temp.path() / name;
    copyFormatFile("plan.v7.json", folder / "plan.json");
    copyFormatFile("project.v6.json", folder / "project.json");
    return folder;
}

// The names of the files in a project's backup folder, sorted.
std::vector<std::string> backupNames(const std::filesystem::path& folder) {
    std::vector<std::string> names;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(folder / "backup", ignored)) {
        names.push_back(utf8(entry.path().filename()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

}

TEST_CASE("version answers with plan format 8 and project format 7", "[cli][format]") {
    const auto outcome = plynth::cli::testing::runChecked({"version"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("plan_format") == 8);
    CHECK(outcome.answer.at("result").at("project_format") == 7);
    CHECK(plynth::kPlanFormatVersion == 8);
    CHECK(plynth::kProjectFormatVersion == 7);
}

TEST_CASE("a new project is written as plan 8 and project 7 with the new fields", "[cli][format]") {
    Sample sample("format-new");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2"}).exitCode == kExitOk);
    const auto plan = readJson(sample.folder / "plan.json");
    CHECK(plan.at("version") == 8);
    const auto& floor = plan.at("floors").at(0);
    CHECK(floor.at("lights").is_array());
    CHECK(floor.at("rooms").at(0).at("dressing") == true);
    CHECK(floor.at("openings").at(0).at("auto_frame") == true);
    CHECK(floor.at("walls").at(0).at("cladding").is_array());
    CHECK(floor.at("decks").is_array());
    CHECK(plan.contains("site"));
    CHECK(plan.contains("foundation"));
    const auto project = readJson(sample.folder / "project.json");
    CHECK(project.at("version") == 7);
    CHECK(project.at("materials").at("ground") == "grass");
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
    CHECK(info.answer.at("result").at("plan").at("version") == 8);
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

TEST_CASE("the first edit of plan 6 and project 5 files saves plan 8 and project 7", "[cli][format]") {
    const Sample sample("format-old-edit");
    makeOlder(sample);
    REQUIRE(sample.run({"point", "add", "--x", "8", "--y", "8"}).exitCode == kExitOk);
    const auto plan = readJson(sample.folder / "plan.json");
    CHECK(plan.at("version") == 8);
    const auto& floor = plan.at("floors").at(0);
    CHECK(floor.at("lights").empty());
    CHECK(floor.at("rooms").at(0).at("dressing") == true);
    CHECK(floor.at("openings").at(0).at("auto_frame") == true);
    CHECK(floor.at("openings").at(0).at("fill").is_null());
    CHECK(floor.at("walls").at(0).at("cladding").empty());
    for (const char* list : {"decks", "railings", "canopies", "dormers", "light_strips"}) {
        CHECK(floor.at(list).empty());
    }
    CHECK(plan.at("ground_floor").is_null());
    CHECK(plan.contains("site"));
    CHECK(plan.contains("foundation"));
    const auto project = readJson(sample.folder / "project.json");
    CHECK(project.at("version") == 7);
    CHECK(project.at("materials").at("ground") == "grass");
    CHECK(project.at("materials").at("boundary_wall") == "red_brick");
    CHECK(project.contains("trim"));
    CHECK(project.contains("variation"));
}

TEST_CASE("a plan of version 9 is refused with unsupported_version and its reason", "[cli][format]") {
    Sample sample("format-newer-plan");
    nlohmann::json plan = readJson(sample.folder / "plan.json");
    plan["version"] = 9;
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

TEST_CASE("a project file of version 8 is refused with unsupported_version", "[cli][format]") {
    Sample sample("format-newer-project");
    nlohmann::json project = readJson(sample.folder / "project.json");
    project["version"] = 8;
    writeJson(sample.folder / "project.json", project);
    const std::string before = readFile(sample.folder / "project.json");

    const auto outcome = sample.run({"variation", "set", "--seed", "3"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "unsupported_version");
    CHECK_FALSE(outcome.answer.at("error").at("message").get<std::string>().empty());
    CHECK(readFile(sample.folder / "project.json") == before);
}

TEST_CASE("the first save of a version 7 plan lists the kept files once", "[cli][format][backup]") {
    const TempFolder temp("format-backup");
    const std::filesystem::path folder = makeVersionSevenProject(temp, "old7");
    const std::string project = utf8(folder);

    const auto first = runChecked({"point", "add", "--x", "40", "--y", "40", "--project", project});
    REQUIRE(first.exitCode == kExitOk);
    const std::vector<std::string> expected = {utf8(folder / "backup" / "plan.v7.json"),
                                               utf8(folder / "backup" / "project.v6.json")};
    REQUIRE(first.answer.at("result").contains("backup"));
    CHECK(ids(first.answer.at("result").at("backup")) == expected);
    CHECK(readFile(folder / "backup" / "plan.v7.json") == readFile(formatsDir() / "plan.v7.json"));
    CHECK(readFile(folder / "backup" / "project.v6.json") == readFile(formatsDir() / "project.v6.json"));
    CHECK(readJson(folder / "plan.json").at("version") == 8);

    const auto second = runChecked({"point", "add", "--x", "41", "--y", "41", "--project", project});
    REQUIRE(second.exitCode == kExitOk);
    CHECK_FALSE(second.answer.at("result").contains("backup"));
    CHECK(backupNames(folder) == std::vector<std::string>{"plan.v7.json", "project.v6.json"});
}

TEST_CASE("a current project saves with no backup list and no backup folder", "[cli][format][backup]") {
    const Sample sample("format-backup-none");
    const auto added = sample.run({"point", "add", "--x", "2", "--y", "2"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK_FALSE(added.answer.at("result").contains("backup"));
    CHECK_FALSE(std::filesystem::exists(sample.folder / "backup"));
}

TEST_CASE("a copy of a sample saved by 0.1.1 keeps its older files on an export options write", "[cli][format][backup]") {
    const TempFolder temp("format-backup-sample");
    const std::filesystem::path source = formatsDir() / "saved-0.1.1" / "house";
    const std::filesystem::path folder = temp.path() / "house";
    std::filesystem::copy(source, folder, std::filesystem::copy_options::recursive);
    const std::string project = utf8(folder);

    const auto off = runChecked({"export", "licenses", "off", "--project", project});
    REQUIRE(off.exitCode == kExitOk);
    CHECK(off.answer.at("result").at("enabled") == false);
    const std::vector<std::string> expected = {utf8(folder / "backup" / "plan.v7.json"),
                                               utf8(folder / "backup" / "project.v6.json")};
    REQUIRE(off.answer.at("result").contains("backup"));
    CHECK(ids(off.answer.at("result").at("backup")) == expected);
    CHECK(readFile(folder / "backup" / "plan.v7.json") == readFile(source / "plan.json"));
    CHECK(readFile(folder / "backup" / "project.v6.json") == readFile(source / "project.json"));

    const auto edit = runChecked({"project", "set", "--grid", "0.25", "--project", project});
    REQUIRE(edit.exitCode == kExitOk);
    CHECK_FALSE(edit.answer.at("result").contains("backup"));
    CHECK(readJson(folder / "plan.json").at("version") == 8);
    CHECK(readJson(folder / "project.json").at("version") == 7);
    CHECK(backupNames(folder) == std::vector<std::string>{"plan.v7.json", "project.v6.json"});
}

TEST_CASE("project recover lists the older files it kept", "[cli][format][backup]") {
    const TempFolder temp("format-backup-recover");
    const std::filesystem::path folder = makeVersionSevenProject(temp, "old7");
    copyFormatFile("plan.v7.json", folder / "autosave" / "plan.json");
    copyFormatFile("project.v6.json", folder / "autosave" / "project.json");

    const auto recovered = runChecked({"project", "recover", "--project", utf8(folder)});
    REQUIRE(recovered.exitCode == kExitOk);
    CHECK(recovered.answer.at("result").at("recovered") == true);
    REQUIRE(recovered.answer.at("result").contains("backup"));
    CHECK(ids(recovered.answer.at("result").at("backup")) ==
          std::vector<std::string>{utf8(folder / "backup" / "plan.v7.json"), utf8(folder / "backup" / "project.v6.json")});
    CHECK_FALSE(std::filesystem::exists(folder / "autosave"));
}
