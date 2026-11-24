#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

using SettingsReset = plynth::cli::testing::GuardedSettingsReset;

// The command words help lists.
std::vector<std::string> helpCommands() {
    const Outcome outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    return names;
}

}

TEST_CASE("project save-as writes the project into a new folder and leaves the first one", "[cli][gaps]") {
    Sample sample("gaps-save-as");
    sample.drawSquare();
    const auto target = sample.temp.path() / std::filesystem::path(u8"Kopie ä");
    const Outcome saved = sample.run({"project", "save-as", utf8(target)});
    REQUIRE(saved.exitCode == kExitOk);
    CHECK(plynth::pathFromUtf8(saved.answer.at("result").at("folder").get<std::string>()) == target);
    CHECK(plynth::isProjectFolder(target));
    CHECK(plynth::loadProject(target).project.plan.floors[0].walls.size() == 4);
    CHECK(std::filesystem::exists(target / "plans"));
    CHECK(plynth::loadProject(sample.folder).project.plan.floors[0].walls.size() == 4);

    const Outcome again = sample.run({"project", "save-as", utf8(target)});
    CHECK(again.exitCode == kExitFailed);
    CHECK(errorCode(again) == "already_exists");
}

TEST_CASE("project recover saves or discards the autosave", "[cli][gaps]") {
    Sample sample("gaps-recover");
    Outcome outcome = sample.run({"project", "recover"});
    CHECK(errorCode(outcome) == "not_found");

    plynth::ProjectLoad load = plynth::loadProject(sample.folder);
    REQUIRE(load.status.ok());
    plynth::Project changed = load.project;
    changed.settings.gridSize = 0.25;
    REQUIRE(plynth::writeAutosave(changed, sample.folder).ok());
    outcome = sample.run({"project", "recover", "--discard"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("discarded") == true);
    CHECK(outcome.answer.at("result").at("has_autosave") == false);
    CHECK(plynth::loadProject(sample.folder).project.settings.gridSize == load.project.settings.gridSize);

    REQUIRE(plynth::writeAutosave(changed, sample.folder).ok());
    outcome = sample.run({"project", "recover"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("recovered") == true);
    CHECK(plynth::loadProject(sample.folder).project.settings.gridSize == 0.25);
}

TEST_CASE("build report, build cancel and export cancel answer without a running job", "[cli][gaps]") {
    Sample sample("gaps-build-words");
    Outcome outcome = sample.run({"build", "report"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK(outcome.answer.at("command") == "build");

    outcome = sample.run({"build", "cancel"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("requested") == false);
    outcome = sample.run({"export", "cancel"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("requested") == false);
    CHECK_FALSE(std::filesystem::exists(sample.folder / "build" / "cancel.request"));

    TempFolder empty("gaps-not-project");
    outcome = runChecked({"build", "cancel", "--project", utf8(empty.path())});
    CHECK(errorCode(outcome) == "not_a_project");
    outcome = runChecked({"build", "stop", "--project", utf8(empty.path())});
    CHECK(errorCode(outcome) == "bad_arguments");
}

TEST_CASE("materials from-asset and library add-sample-materials say why they refuse", "[cli][gaps]") {
    SettingsReset reset;
    TempFolder temp("gaps-materials");
    const auto library = temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);
    Outcome outcome = runChecked({"materials", "from-asset", "ghost", "Seat", "--library", utf8(library)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    outcome = runChecked({"materials", "from-asset", "ghost", "--library", utf8(library)});
    CHECK(errorCode(outcome) == "bad_arguments");
    outcome = runChecked({"library", "add-sample-materials", "--library", utf8(temp.path() / "nolibrary")});
    CHECK(errorCode(outcome) == "no_library");
}

TEST_CASE("help lists the new commands", "[cli][gaps]") {
    const std::vector<std::string> names = helpCommands();
    for (const char* wanted : {"project save-as", "project recover", "materials from-asset", "library add-sample-materials",
                               "pack", "unpack", "render", "build", "export"}) {
        INFO(wanted);
        CHECK(std::find(names.begin(), names.end(), std::string(wanted)) != names.end());
    }
}
