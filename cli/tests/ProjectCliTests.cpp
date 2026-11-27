#include "TestSupport.h"

#include <plynth/cli/Cli.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// The whole content of a file.
std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The code of a failed answer.
std::string errorCode(const Outcome& outcome) {
    return outcome.answer.at("error").at("code").get<std::string>();
}

// Makes a project with the sample image in a new temp folder and returns its path.
std::filesystem::path makeSampleProject(const TempFolder& temp) {
    const auto folder = temp.path() / "flat";
    const auto outcome = runChecked({"project", "new", "--image", PLYNTH_SAMPLE_PLAN, "--project", utf8(folder)});
    REQUIRE(outcome.exitCode == kExitOk);
    return folder;
}

}

TEST_CASE("project new with an image creates the project files", "[cli][project]") {
    TempFolder temp("new");
    const auto folder = temp.path() / "flat";
    const auto outcome = runChecked({"project", "new", "--image", PLYNTH_SAMPLE_PLAN, "--project", utf8(folder),
                                     "--export", "grouped", "--grid", "0.25"});
    CHECK(outcome.exitCode == kExitOk);
    CHECK(std::filesystem::exists(folder / "project.json"));
    CHECK(std::filesystem::exists(folder / "plan.json"));
    CHECK(std::filesystem::exists(folder / "plans" / "apartment-01.png"));

    const auto& result = outcome.answer.at("result");
    CHECK(result.at("name") == "flat");
    CHECK(result.at("export_mode") == "grouped");
    CHECK(result.at("grid_size") == 0.25);
    CHECK(result.at("has_autosave") == false);
    CHECK(result.at("plan").at("pixels_per_metre").is_null());
    const auto& floor = result.at("plan").at("floors").at(0);
    CHECK(floor.at("id") == "f1");
    CHECK(floor.at("image") == "plans/apartment-01.png");
    CHECK(floor.at("image_found") == true);
    CHECK(floor.at("points") == 0);
}

TEST_CASE("project new uses the current folder and a given name", "[cli][project]") {
    TempFolder temp("cwd");
    const auto outcome = runChecked({"project", "new", "--name", "My flat"}, temp.path());
    CHECK(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("name") == "My flat");
    CHECK(std::filesystem::exists(temp.path() / "project.json"));
    CHECK(outcome.answer.at("result").at("plan").at("floors").at(0).at("image").is_null());
}

TEST_CASE("project new twice in one folder gives already_exists", "[cli][project]") {
    TempFolder temp("twice");
    const auto folder = makeSampleProject(temp);
    const auto before = readFile(folder / "plan.json");
    const auto again = runChecked({"project", "new", "--project", utf8(folder)});
    CHECK(again.exitCode == kExitFailed);
    CHECK(errorCode(again) == "already_exists");
    CHECK(readFile(folder / "plan.json") == before);
}

TEST_CASE("project new accepts an empty folder that is already there", "[cli][project]") {
    TempFolder temp("empty");
    const auto folder = temp.path() / "flat";
    std::filesystem::create_directories(folder);
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitOk);
    CHECK(std::filesystem::exists(folder / "project.json"));
}

TEST_CASE("project new refuses a folder with a file in it and leaves the file alone", "[cli][project]") {
    TempFolder temp("full");
    const auto folder = temp.path() / "flat";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "notes.txt", std::ios::binary) << "mine\n";
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "already_exists");
    CHECK(readFile(folder / "notes.txt") == "mine\n");
    CHECK_FALSE(std::filesystem::exists(folder / "project.json"));
    CHECK_FALSE(std::filesystem::exists(folder / "AGENTS.md"));
}

TEST_CASE("project new refuses a folder with only an empty folder in it", "[cli][project]") {
    TempFolder temp("subfolder");
    const auto folder = temp.path() / "flat";
    std::filesystem::create_directories(folder / "old");
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "already_exists");
    CHECK_FALSE(std::filesystem::exists(folder / "project.json"));
}

TEST_CASE("project new refuses a path that is a file", "[cli][project]") {
    TempFolder temp("file");
    const auto file = temp.path() / "flat";
    std::ofstream(file, std::ios::binary) << "mine\n";
    const auto outcome = runChecked({"project", "new", "--project", utf8(file)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "already_exists");
    CHECK(readFile(file) == "mine\n");
}

TEST_CASE("project new refuses a name Windows keeps for itself", "[cli][project]") {
    TempFolder temp("reserved");
    for (const std::string name : {"CON", "nul", "com1", "LPT9", "aux.txt"}) {
        INFO(name);
        const auto outcome = runChecked({"project", "new", "--project", utf8(temp.path()) + "/" + name});
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "invalid_value");
    }
    CHECK(std::filesystem::is_empty(temp.path()));
}

TEST_CASE("project new refuses a name with a character Windows does not allow", "[cli][project]") {
    TempFolder temp("badchar");
    for (const std::string name : {"fl|at", "fl*at", "fl?at", "fl<at", "fl\"at"}) {
        INFO(name);
        const auto outcome = runChecked({"project", "new", "--project", utf8(temp.path()) + "/" + name});
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "invalid_value");
    }
    CHECK(std::filesystem::is_empty(temp.path()));
}

TEST_CASE("project new refuses a name that ends with a dot or a space", "[cli][project]") {
    TempFolder temp("ending");
    for (const std::string name : {"flat.", "flat "}) {
        INFO(name);
        const auto outcome = runChecked({"project", "new", "--project", utf8(temp.path()) + "/" + name});
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "invalid_value");
    }
    CHECK(std::filesystem::is_empty(temp.path()));
}

TEST_CASE("project new with a missing image gives image_error and writes nothing", "[cli][project]") {
    TempFolder temp("noimg");
    const auto folder = temp.path() / "flat";
    const auto outcome = runChecked(
        {"project", "new", "--image", utf8(temp.path() / "none.png"), "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "image_error");
    CHECK_FALSE(std::filesystem::exists(folder));
}

TEST_CASE("project new with a bad export mode or grid size is refused", "[cli][project]") {
    TempFolder temp("bad");
    const auto mode = runChecked({"project", "new", "--export", "pairs"}, temp.path());
    CHECK(mode.exitCode == kExitBadArguments);
    CHECK(errorCode(mode) == "bad_arguments");
    const auto grid = runChecked({"project", "new", "--grid", "0"}, temp.path());
    CHECK(grid.exitCode == kExitFailed);
    CHECK(errorCode(grid) == "invalid_value");
    CHECK_FALSE(std::filesystem::exists(temp.path() / "project.json"));
}

TEST_CASE("project info on a folder without a project gives not_a_project", "[cli][project]") {
    TempFolder temp("info");
    const auto outcome = runChecked({"project", "info"}, temp.path());
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_a_project");
}

TEST_CASE("project info reports the project and --project works with a relative folder", "[cli][project]") {
    TempFolder temp("rel");
    makeSampleProject(temp);
    const auto outcome = runChecked({"project", "info", "--project", "flat"}, temp.path());
    CHECK(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("name") == "flat");
    CHECK(outcome.answer.at("result").at("folder").get<std::string>().find('\\') == std::string::npos);
}

TEST_CASE("project info reports a broken plan.json", "[cli][project]") {
    TempFolder temp("broken");
    const auto folder = makeSampleProject(temp);
    std::ofstream(folder / "plan.json") << "{ not json";
    const auto outcome = runChecked({"project", "info", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "bad_file");
}

TEST_CASE("points and a wall get the first ids and are saved", "[cli][edit]") {
    TempFolder temp("edit");
    const auto folder = makeSampleProject(temp);
    const std::string project = utf8(folder);

    const auto p1 = runChecked({"point", "add", "--x", "1.5", "--y", "2", "--project", project});
    CHECK(p1.exitCode == kExitOk);
    CHECK(p1.answer.at("result").at("id") == "p1");
    CHECK(p1.answer.at("result").at("floor") == "f1");
    CHECK(p1.answer.at("result").at("x") == 1.5);
    CHECK(p1.answer.at("result").at("corner_radius") == 0.0);
    const auto p2 =
        runChecked({"point", "add", "--x", "-4", "--y", "2", "--corner-radius", "0.1", "--project", project});
    CHECK(p2.answer.at("result").at("id") == "p2");
    CHECK(p2.answer.at("result").at("x") == -4.0);

    const auto wall = runChecked({"wall", "add", "--from", "p1", "--to", "p2", "--project", project});
    CHECK(wall.exitCode == kExitOk);
    CHECK(wall.answer.at("result").at("id") == "w1");
    CHECK(wall.answer.at("result").at("thickness") == 0.2);
    CHECK(wall.answer.at("result").at("height").is_null());

    const auto plan = readFile(folder / "plan.json");
    CHECK(plan.find("\"p1\"") != std::string::npos);
    CHECK(plan.find("\"p2\"") != std::string::npos);
    CHECK(plan.find("\"w1\"") != std::string::npos);

    const auto info = runChecked({"project", "info", "--project", project});
    const auto& floor = info.answer.at("result").at("plan").at("floors").at(0);
    CHECK(floor.at("points") == 2);
    CHECK(floor.at("walls") == 1);
}

TEST_CASE("wall add with an unknown point gives not_found and leaves plan.json unchanged", "[cli][edit]") {
    TempFolder temp("unknown");
    const auto folder = makeSampleProject(temp);
    const std::string project = utf8(folder);
    runChecked({"point", "add", "--x", "0", "--y", "0", "--project", project});
    const auto before = readFile(folder / "plan.json");
    const auto outcome = runChecked({"wall", "add", "--from", "p1", "--to", "p9", "--project", project});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK(outcome.answer.at("command") == "wall add");
    CHECK(readFile(folder / "plan.json") == before);
}

TEST_CASE("scale set stores the scale and refuses zero", "[cli][edit]") {
    TempFolder temp("scale");
    const auto folder = makeSampleProject(temp);
    const std::string project = utf8(folder);
    const auto good = runChecked({"scale", "set", "--pixels-per-metre", "52.4", "--project", project});
    CHECK(good.exitCode == kExitOk);
    CHECK(good.answer.at("result").at("pixels_per_metre") == 52.4);

    const auto before = readFile(folder / "plan.json");
    const auto zero = runChecked({"scale", "set", "--pixels-per-metre", "0", "--project", project});
    CHECK(zero.exitCode == kExitFailed);
    CHECK(errorCode(zero) == "invalid_value");
    CHECK(readFile(folder / "plan.json") == before);
}

TEST_CASE("missing required options give bad_arguments", "[cli][edit]") {
    TempFolder temp("req");
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"scale", "set"}, {"point", "add", "--x", "1"}, {"wall", "add", "--from", "p1"}}) {
        const auto outcome = runChecked(args, temp.path());
        CHECK(outcome.exitCode == kExitBadArguments);
        CHECK(errorCode(outcome) == "bad_arguments");
    }
}

TEST_CASE("edits on a folder without a project give not_a_project", "[cli][edit]") {
    TempFolder temp("none");
    const auto outcome = runChecked({"point", "add", "--x", "1", "--y", "1"}, temp.path());
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_a_project");
}

TEST_CASE("help lists the project and edit commands with their options", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
        if (names.back() == "project new") {
            CHECK(command.at("options").size() == 6);
        }
    }
    for (const char* wanted : {"project new", "project info", "scale set", "point add", "wall add"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
}

TEST_CASE("project set --export changes the export mode and undoes nothing else", "[cli][project]") {
    TempFolder temp("set-export");
    const auto folder = makeSampleProject(temp);
    const auto before = runChecked({"project", "info", "--project", utf8(folder)});
    CHECK(before.answer.at("result").at("export_mode") == "single");

    const auto set = runChecked({"project", "set", "--export", "grouped", "--project", utf8(folder)});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("export_mode") == "grouped");
    CHECK(set.answer.at("result").at("grid_size") == before.answer.at("result").at("grid_size"));

    const auto back = runChecked({"project", "set", "--export", "single", "--project", utf8(folder)});
    CHECK(back.answer.at("result").at("export_mode") == "single");

    const auto bad = runChecked({"project", "set", "--export", "pairs", "--project", utf8(folder)});
    CHECK(bad.exitCode != kExitOk);
}
