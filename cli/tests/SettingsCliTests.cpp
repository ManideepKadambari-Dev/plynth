#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// Removes the settings file so a test starts from a first run.
void clearSettings() {
    plynth::cli::testing::removeSettingsFileSafely();
}

}

TEST_CASE("settings show without a file gives the defaults", "[cli][settings]") {
    clearSettings();
    const auto outcome = runChecked({"settings", "show"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("exists") == false);
    CHECK(result.at("file").get<std::string>().find("settings.json") != std::string::npos);
    const auto& settings = result.at("settings");
    CHECK(settings.at("library").is_null());
    CHECK(settings.at("projects").is_null());
    CHECK(settings.at("keep_imported_zips") == false);
    CHECK_FALSE(settings.contains("thumbnail_engine"));
    CHECK(settings.at("triangle_warning") == 500000);
    CHECK_FALSE(settings.contains("format"));
    CHECK_FALSE(settings.contains("version"));
}

TEST_CASE("settings set saves and settings show reads it back", "[cli][settings]") {
    clearSettings();
    TempFolder temp("settings-projects");
    const auto set = runChecked({"settings", "set", "--projects", utf8(temp.path()), "--keep-zips", "true",
                                 "--triangle-warning", "1000"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("exists") == true);

    const auto shown = runChecked({"settings", "show"});
    const auto& settings = shown.answer.at("result").at("settings");
    CHECK(settings.at("projects") == utf8(temp.path()));
    CHECK(settings.at("keep_imported_zips") == true);
    CHECK_FALSE(settings.contains("thumbnail_engine"));
    CHECK(settings.at("triangle_warning") == 1000);
    const auto saved = nlohmann::json::parse(readFile(plynth::settingsFile()));
    CHECK(saved.at("version") == plynth::kSettingsFormatVersion);
    CHECK_FALSE(saved.contains("thumbnail_engine"));

    const auto cleared = runChecked({"settings", "set", "--projects", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("settings").at("projects").is_null());
    clearSettings();
}

TEST_CASE("settings set with a missing folder is refused and the file stays as it was", "[cli][settings]") {
    clearSettings();
    TempFolder temp("settings-missing");
    REQUIRE(runChecked({"settings", "set", "--keep-zips", "true"}).exitCode == kExitOk);
    const std::string before = readFile(plynth::settingsFile());

    const auto missing = runChecked({"settings", "set", "--projects", utf8(temp.path() / "nope"), "--keep-zips", "false"});
    CHECK(missing.exitCode != kExitOk);
    CHECK(errorCode(missing) == "not_found");
    CHECK(readFile(plynth::settingsFile()) == before);
    clearSettings();
}

TEST_CASE("settings set refuses bad values", "[cli][settings]") {
    clearSettings();
    const auto zips = runChecked({"settings", "set", "--keep-zips", "maybe"});
    CHECK(errorCode(zips) == "bad_arguments");
    const auto engine = runChecked({"settings", "set", "--thumbnail-engine", "eevee"});
    CHECK(errorCode(engine) == "bad_arguments");
    const auto nothing = runChecked({"settings", "set"});
    CHECK(errorCode(nothing) == "bad_arguments");
    const auto triangles = runChecked({"settings", "set", "--triangle-warning", "0"});
    CHECK(errorCode(triangles) == "invalid_value");
    const auto program = runChecked({"settings", "set", "--blender", "C:/does/not/exist/blender.exe"});
    CHECK(errorCode(program) == "blender_not_found");
    CHECK_FALSE(std::filesystem::exists(plynth::settingsFile()));
}

TEST_CASE("settings set refuses with the sentences of the app's setup check", "[cli][settings]") {
    clearSettings();
    TempFolder temp("settings-sentences");
    const auto folder = temp.path() / "plain";
    std::filesystem::create_directories(folder);

    const auto library = runChecked({"settings", "set", "--library", utf8(folder)});
    CHECK(errorCode(library) == "no_library");
    CHECK(library.answer.at("error").at("message") ==
          utf8(folder) + " is not a Plynth library. Run plynth library init there, or pick another folder.");
    const auto projects = runChecked({"settings", "set", "--projects", utf8(temp.path() / "nope")});
    CHECK(projects.answer.at("error").at("message") ==
          "The projects folder " + utf8(temp.path() / "nope") + " does not exist.");
    const auto blender = runChecked({"settings", "set", "--blender", utf8(folder / "blender.exe")});
    CHECK(errorCode(blender) == "blender_not_found");
    CHECK(blender.answer.at("error").at("message") == utf8(folder / "blender.exe") + " is not a file.");
    CHECK(runChecked({"settings", "set", "--triangle-warning", "0"}).answer.at("error").at("message") ==
          "The triangle warning must be at least 1.");
    CHECK_FALSE(std::filesystem::exists(plynth::settingsFile()));
}

TEST_CASE("an edit on a project whose plan image is gone succeeds with a warning", "[cli][settings]") {
    Sample sample("image-missing");
    REQUIRE(sample.run({"scale", "set", "--pixels-per-metre", "100"}).exitCode == kExitOk);
    const std::filesystem::path images = sample.folder / "plans";
    std::string imagePath;
    for (const auto& entry : std::filesystem::directory_iterator(images)) {
        imagePath = "plans/" + utf8(entry.path().filename());
        std::filesystem::remove(entry.path());
    }
    REQUIRE_FALSE(imagePath.empty());

    const auto drawn = sample.draw({"--from-x", "1", "--from-y", "1", "--to-x", "5", "--to-y", "1"});
    REQUIRE(drawn.exitCode == kExitOk);
    REQUIRE(drawn.answer.contains("warnings"));
    const auto& warnings = drawn.answer.at("warnings");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].at("code") == "image_missing");
    CHECK(warnings[0].at("message").get<std::string>().find(imagePath) != std::string::npos);

    const std::string plan = readFile(sample.folder / "plan.json");
    CHECK(plan.find(imagePath) != std::string::npos);

    const auto listed = sample.run({"wall", "list"});
    CHECK_FALSE(listed.answer.contains("warnings"));
}

TEST_CASE("project new with a missing image is still refused", "[cli][settings]") {
    TempFolder temp("new-missing");
    const auto outcome = runChecked({"project", "new", "--image", utf8(temp.path() / "none.png"), "--project",
                                     utf8(temp.path() / "p")});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(errorCode(outcome) == "image_error");
    CHECK_FALSE(std::filesystem::exists(temp.path() / "p"));
}
