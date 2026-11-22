#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json-schema.hpp>

#include <filesystem>
#include <fstream>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

using SettingsReset = plynth::cli::testing::GuardedSettingsReset;

// Checks a doctor result against schema/setup-report.schema.json.
void validateReport(const nlohmann::json& result) {
    std::ifstream file(std::filesystem::path(PLYNTH_SCHEMA_DIR) / "setup-report.schema.json");
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(file));
    REQUIRE_NOTHROW(validator.validate(result));
}

// The item of a report with this id.
nlohmann::json item(const nlohmann::json& result, const std::string& id) {
    for (const auto& entry : result.at("items")) {
        if (entry.at("id") == id) {
            return entry;
        }
    }
    INFO("No item " << id);
    REQUIRE(false);
    return {};
}

// Makes a library and a projects folder under temp and saves them in the settings.
void setUpFolders(const TempFolder& temp) {
    std::filesystem::create_directories(temp.path() / "Projects");
    REQUIRE(runChecked({"library", "init", utf8(temp.path() / "Library"), "--use"}).exitCode == kExitOk);
    REQUIRE(runChecked({"settings", "set", "--projects", utf8(temp.path() / "Projects")}).exitCode == kExitOk);
}

}

TEST_CASE("doctor with the fake Blender, folders set and add-ons skipped is ready", "[cli][doctor]") {
    SettingsReset reset;
    TempFolder temp("doctor-ready");
    setUpFolders(temp);
    const auto fake = installFakeBlender(temp.path() / "fb52", "5.2.0");

    const auto outcome = runChecked({"doctor", "--no-addons", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    validateReport(result);
    CHECK(result.at("ready") == true);
    CHECK(result.at("probed") == false);
    CHECK(item(result, "blender").at("state") == "found");
    CHECK(item(result, "blender").at("path") == utf8(fake));
    CHECK(item(result, "blender_version").at("state") == "found");
    CHECK(item(result, "library_folder").at("state") == "found");
    CHECK(item(result, "projects_folder").at("state") == "found");
    CHECK(item(result, "graphics_driver").at("state") == "not_checked");
    CHECK(item(result, "addon_archimesh").at("state") == "not_checked");
}

TEST_CASE("doctor asks the fake Blender about add-ons", "[cli][doctor]") {
    SettingsReset reset;
    TempFolder temp("doctor-addons");
    setUpFolders(temp);
    const auto fake = installFakeBlender(temp.path() / "fb52", "5.2.0");
    const ScopedEnvironment addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.archimesh");
    const ScopedEnvironment kit("FAKE_BLENDER_BLENDERKIT", "enabled");

    const auto outcome = runChecked({"doctor", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    validateReport(result);
    CHECK(result.at("probed") == true);
    CHECK(item(result, "addon_archimesh").at("state") == "found");
    CHECK(item(result, "addon_blenderkit").at("state") == "needs_attention");
    CHECK(result.at("ready") == true);
}

TEST_CASE("doctor with no library or projects folder is not ready", "[cli][doctor]") {
    SettingsReset reset;
    TempFolder temp("doctor-empty");
    const auto fake = installFakeBlender(temp.path() / "fb52", "5.2.0");

    const auto outcome = runChecked({"doctor", "--no-addons", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    validateReport(result);
    CHECK(result.at("ready") == false);
    CHECK(result.at("first_run") == true);
    CHECK(item(result, "library_folder").at("state") == "missing");
    CHECK(item(result, "projects_folder").at("state") == "missing");
}

TEST_CASE("settings set --blender refuses a Blender that is too old and leaves the file alone", "[cli][doctor]") {
    SettingsReset reset;
    TempFolder temp("doctor-settings");
    REQUIRE(runChecked({"settings", "set", "--keep-zips", "true"}).exitCode == kExitOk);
    const std::string before = readFile(plynth::settingsFile());
    const auto old = installFakeBlender(temp.path() / "fb41", "4.1.1");
    const auto good = installFakeBlender(temp.path() / "fb52", "5.2.0");

    const auto refused = runChecked({"settings", "set", "--blender", utf8(old)});
    CHECK(refused.exitCode != kExitOk);
    CHECK(errorCode(refused) == "blender_not_found");
    CHECK(readFile(plynth::settingsFile()) == before);

    const auto accepted = runChecked({"settings", "set", "--blender", utf8(good)});
    REQUIRE(accepted.exitCode == kExitOk);
    CHECK(accepted.answer.at("result").at("settings").at("blender") == utf8(good));
}

TEST_CASE("doctor on this machine finds the real Blender", "[cli][doctor][.blender]") {
    SettingsReset reset;
    const auto outcome = runChecked({"doctor"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    validateReport(result);
    CHECK(result.at("ready").is_boolean());
    CHECK(item(result, "blender").at("state") == "found");
}
