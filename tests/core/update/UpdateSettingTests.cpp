#include "UpdateTestSupport.h"

#include "../SchemaSupport.h"

#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using namespace plynth::test::update;

TEST_CASE("every older settings version reads the update check as on", "[update][settings]") {
    for (int version = 1; version <= 5; ++version) {
        INFO(version);
        MachineSettings settings;
        settings.updateCheck = false;
        plynth::test::requireOk(settingsFromJson(
            Json::parse(R"({"format": "plynth-settings", "version": )" + std::to_string(version) + "}"), settings));
        CHECK(settings.updateCheck);
    }
    CHECK(MachineSettings{}.updateCheck);
    CHECK(kSettingsFormatVersion == 6);
}

TEST_CASE("the update check switch off survives every save", "[update][settings]") {
    plynth::test::TempDir tmp("update-setting-save");
    const fs::path file = tmp.path() / "settings.json";
    MachineSettings settings;
    settings.updateCheck = false;
    plynth::test::requireOk(saveSettings(settings, file));
    SettingsLoad loaded = loadSettings(file);
    plynth::test::requireOk(loaded.status);
    CHECK_FALSE(loaded.settings.updateCheck);

    // Another caller changes something else and saves again.
    addRecentProject(loaded.settings, tmp.path() / "Project", "Project", sessionNow());
    loaded.settings.sidePanelWidth = 300;
    plynth::test::requireOk(saveSettings(loaded.settings, file));
    const SettingsLoad again = loadSettings(file);
    plynth::test::requireOk(again.status);
    CHECK_FALSE(again.settings.updateCheck);
    CHECK(again.settings.sidePanelWidth == 300);

    const Json json = settingsToJson(again.settings);
    CHECK(json.at("update_check") == false);
    CHECK(json.at("version") == 6);
    CHECK(plynth::test::schemaProblem("settings.schema.json", nlohmann::json::parse(json.dump())).empty());
}

TEST_CASE("bad, misplaced and newer update check values are refused", "[update][settings]") {
    MachineSettings settings;
    Status status = settingsFromJson(Json::parse(R"({"format": "plynth-settings", "version": 6, "update_check": "yes"})"), settings);
    CHECK(status.code == ErrorCode::BadFile);
    CHECK(status.where == "/update_check");
    status = settingsFromJson(Json::parse(R"({"format": "plynth-settings", "version": 6, "update_check": null})"), settings);
    CHECK(status.code == ErrorCode::BadFile);
    status = settingsFromJson(Json::parse(R"({"format": "plynth-settings", "version": 5, "update_check": false})"), settings);
    CHECK(status.code == ErrorCode::BadFile);
    CHECK(status.where == "/update_check");
    status = settingsFromJson(Json::parse(R"({"format": "plynth-settings", "version": 7, "update_check": false})"), settings);
    CHECK(status.code == ErrorCode::UnsupportedVersion);
}

TEST_CASE("the settings schemas agree on the update check", "[update][settings][schema]") {
    const auto off = nlohmann::json::parse(R"({"format": "plynth-settings", "version": 6, "update_check": false})");
    CHECK(plynth::test::schemaProblem("settings.schema.json", off).empty());
    const auto bad = nlohmann::json::parse(R"({"format": "plynth-settings", "version": 6, "update_check": "no"})");
    CHECK_FALSE(plynth::test::schemaProblem("settings.schema.json", bad).empty());
    const auto oldWithSwitch = nlohmann::json::parse(R"({"format": "plynth-settings", "version": 5, "update_check": false})");
    CHECK_FALSE(plynth::test::schemaProblem("settings.v5.schema.json", oldWithSwitch).empty());
    const auto old = nlohmann::json::parse(R"({"format": "plynth-settings", "version": 5, "side_panel": {"open": true}})");
    CHECK(plynth::test::schemaProblem("settings.v5.schema.json", old).empty());
    CHECK_FALSE(plynth::test::schemaProblem("settings.schema.json", old).empty());
}
