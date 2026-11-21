#include "SampleProject.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// Clears the settings file when a test starts and when it ends.
struct SettingsReset {
    SettingsReset() { clear(); }
    ~SettingsReset() { clear(); }
    static void clear() {
        std::error_code ignored;
        std::filesystem::remove(plynth::settingsFile(), ignored);
    }
};

// Saves a recent list with a project that exists and one whose folder is gone.
void writeRecent(const std::filesystem::path& here, const std::filesystem::path& gone) {
    plynth::MachineSettings settings;
    plynth::addRecentProject(settings, gone, "Gone", plynth::sessionNow());
    plynth::addRecentProject(settings, here, "Flat", plynth::sessionNow());
    REQUIRE(plynth::saveSettings(settings).ok());
}

}

TEST_CASE("project recent lists the projects with an exists flag", "[cli][project]") {
    SettingsReset reset;
    const TempFolder temp("recent-list");
    const auto empty = runChecked({"project", "recent"});
    REQUIRE(empty.exitCode == kExitOk);
    CHECK(empty.answer.at("result").at("count") == 0);

    const auto here = temp.path() / "flat";
    std::filesystem::create_directories(here);
    writeRecent(here, temp.path() / "gone");
    const auto listed = runChecked({"project", "recent"});
    REQUIRE(listed.exitCode == kExitOk);
    const auto& projects = listed.answer.at("result").at("projects");
    REQUIRE(projects.size() == 2);
    CHECK(projects.at(0).at("name") == "Flat");
    CHECK(projects.at(0).at("path") == utf8(here));
    CHECK(projects.at(0).at("exists") == true);
    CHECK(projects.at(0).at("last_opened").is_string());
    CHECK(projects.at(1).at("name") == "Gone");
    CHECK(projects.at(1).at("exists") == false);
}

TEST_CASE("project recent --remove takes one entry off and keeps the folder", "[cli][project]") {
    SettingsReset reset;
    const TempFolder temp("recent-remove");
    const auto here = temp.path() / "flat";
    std::filesystem::create_directories(here);
    writeRecent(here, temp.path() / "gone");

    const auto removed = runChecked({"project", "recent", "--remove", utf8(here)});
    REQUIRE(removed.exitCode == kExitOk);
    CHECK(removed.answer.at("result").at("removed") == utf8(here));
    CHECK(removed.answer.at("result").at("count") == 1);
    CHECK(std::filesystem::exists(here));
    const auto listed = runChecked({"project", "recent"});
    CHECK(listed.answer.at("result").at("projects").at(0).at("name") == "Gone");

    const auto again = runChecked({"project", "recent", "--remove", utf8(here)});
    CHECK(again.exitCode == kExitFailed);
    CHECK(errorCode(again) == "not_found");
}

TEST_CASE("opening and saving through the CLI does not add to the recent list", "[cli][project]") {
    SettingsReset reset;
    const Sample sample("recent-cli");
    REQUIRE(sample.run({"scale", "set", "--pixels-per-metre", "100"}).exitCode == kExitOk);
    REQUIRE(sample.run({"project", "info"}).exitCode == kExitOk);
    const auto listed = runChecked({"project", "recent"});
    CHECK(listed.answer.at("result").at("count") == 0);
}
