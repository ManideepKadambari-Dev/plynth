#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// The path of a settings test data file.
fs::path dataFile(const std::string& name)
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / name;
}

}

TEST_CASE("settings round trip", "[core][settings]") {
    plynth::test::TempDir tmp("settings-roundtrip");
    MachineSettings s;
    s.library = fs::path(tmp.path() / "Lib");
    s.projects = fs::path(tmp.path() / "Projects");
    s.blender = fs::path("C:/Blender/blender.exe");
    s.keepImportedZips = true;
    s.thumbnailEngine = ThumbnailEngine::Cycles;
    s.triangleWarning = 12345;
    s.setupCheckedVersion = "1.2.3";

    const fs::path file = tmp.path() / "config" / "settings.json";
    plynth::test::requireOk(saveSettings(s, file));
    const SettingsLoad loaded = loadSettings(file);
    plynth::test::requireOk(loaded.status);
    REQUIRE(loaded.exists);
    REQUIRE(loaded.settings == s);
    REQUIRE_FALSE(fs::exists(fs::path(file) += ".tmp"));
}

TEST_CASE("a missing settings file is ok and means first run", "[core][settings]") {
    plynth::test::TempDir tmp("settings-missing");
    const SettingsLoad loaded = loadSettings(tmp.path() / "settings.json");
    REQUIRE(loaded.status.ok());
    REQUIRE_FALSE(loaded.exists);
    REQUIRE(loaded.settings == MachineSettings{});
    REQUIRE_FALSE(loaded.settings.library);
    REQUIRE(loaded.settings.triangleWarning == kDefaultTriangleWarning);
}

TEST_CASE("settings data files", "[core][settings]") {
    SECTION("a full file") {
        const SettingsLoad l = loadSettings(dataFile("settings-valid.json"));
        plynth::test::requireOk(l.status);
        REQUIRE(l.settings.library == fs::path("D:/Lib"));
        REQUIRE_FALSE(l.settings.projects);
        REQUIRE(l.settings.keepImportedZips);
        REQUIRE(l.settings.thumbnailEngine == ThumbnailEngine::Cycles);
        REQUIRE(l.settings.triangleWarning == 1000);
        REQUIRE(l.settings.setupCheckedVersion == "0.6.0");
    }
    SECTION("only the required fields takes the defaults") {
        const SettingsLoad l = loadSettings(dataFile("settings-minimal.json"));
        plynth::test::requireOk(l.status);
        REQUIRE(l.settings == MachineSettings{});
    }
    SECTION("an unknown field is bad_file with a pointer") {
        const SettingsLoad l = loadSettings(dataFile("settings-unknown-field.json"));
        REQUIRE(l.status.code == ErrorCode::BadFile);
        REQUIRE(l.status.where == "/colour");
        REQUIRE(l.exists);
    }
    SECTION("version 2 is unsupported_version") {
        const SettingsLoad l = loadSettings(dataFile("settings-version-2.json"));
        REQUIRE(l.status.code == ErrorCode::UnsupportedVersion);
    }
    SECTION("a bad value points at its field") {
        const SettingsLoad l = loadSettings(dataFile("settings-bad-engine.json"));
        REQUIRE(l.status.code == ErrorCode::BadFile);
        REQUIRE(l.status.where == "/thumbnail_engine");
    }
}

TEST_CASE("bad settings files", "[core][settings]") {
    plynth::test::TempDir tmp("settings-bad");
    const auto writeAndLoad = [&](const std::string& text) {
        const fs::path file = tmp.path() / "settings.json";
        std::ofstream(file, std::ios::binary) << text;
        return loadSettings(file);
    };
    REQUIRE(writeAndLoad("{not json").status.code == ErrorCode::BadFile);
    REQUIRE(writeAndLoad("[]").status.code == ErrorCode::BadFile);
    REQUIRE(writeAndLoad(R"({"format":"other","version":1})").status.where == "/format");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings"})").status.where == "/version");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":1,"library":""})").status.where == "/library");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":1,"triangle_warning":0})").status.where ==
            "/triangle_warning");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":1,"keep_imported_zips":"yes"})").status.where ==
            "/keep_imported_zips");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":1,"setup_checked_version":3})").status.where ==
            "/setup_checked_version");
}

TEST_CASE("settingsToJson validates against the schema", "[core][settings][schema]") {
    const auto plain = [](const Json& j) { return nlohmann::json::parse(j.dump()); };
    REQUIRE(plynth::test::schemaProblem("settings.schema.json", plain(settingsToJson(MachineSettings{}))).empty());

    MachineSettings s;
    s.library = fs::path("D:/Library ü");
    s.projects = fs::path("D:/Projects");
    s.blender = fs::path("C:/Program Files/Blender Foundation/Blender 5.2/blender.exe");
    s.keepImportedZips = true;
    s.thumbnailEngine = ThumbnailEngine::Cycles;
    s.setupCheckedVersion = "0.6.0";
    REQUIRE(plynth::test::schemaProblem("settings.schema.json", plain(settingsToJson(s))).empty());

    for (const char* name : {"settings-valid.json", "settings-minimal.json"}) {
        INFO(name);
        const auto json = nlohmann::json::parse(plynth::test::readTextFile(dataFile(name)));
        REQUIRE(plynth::test::schemaProblem("settings.schema.json", json).empty());
    }
    for (const char* name : {"settings-unknown-field.json", "settings-version-2.json", "settings-bad-engine.json"}) {
        INFO(name);
        const auto json = nlohmann::json::parse(plynth::test::readTextFile(dataFile(name)));
        REQUIRE_FALSE(plynth::test::schemaProblem("settings.schema.json", json).empty());
    }
}

TEST_CASE("PLYNTH_CONFIG_DIR changes the settings file", "[core][settings]") {
    // The variable is set in the ANSI code page, so this folder name stays plain ASCII.
    const fs::path base = fs::temp_directory_path() / "plynth-settings-env";
    std::error_code ignore;
    fs::remove_all(base, ignore);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code e; fs::remove_all(path, e); }
    } cleanup{base};
    {
        plynth::test::ScopedEnv env("PLYNTH_CONFIG_DIR", pathToUtf8(base / "cfg"));
        REQUIRE(settingsFolder() == base / "cfg");
        REQUIRE(settingsFile() == base / "cfg" / "settings.json");
        MachineSettings s;
        s.keepImportedZips = true;
        plynth::test::requireOk(saveSettings(s));
        REQUIRE(fs::exists(base / "cfg" / "settings.json"));
        REQUIRE(loadSettings().settings.keepImportedZips);
    }
    REQUIRE(settingsFolder() != base / "cfg");
}

TEST_CASE("without PLYNTH_CONFIG_DIR the folder is Plynth under the user's AppData", "[core][settings]") {
    plynth::test::ScopedEnv env("PLYNTH_CONFIG_DIR", "");
    const fs::path folder = settingsFolder();
    REQUIRE(folder.filename() == "Plynth");
    REQUIRE(folder.string().find("AppData") != std::string::npos);
}

TEST_CASE("default folders sit under Documents\\Plynth", "[core][settings]") {
    const fs::path library = defaultLibraryFolder();
    const fs::path projects = defaultProjectsFolder();
    REQUIRE(library.filename() == "Library");
    REQUIRE(projects.filename() == "Projects");
    REQUIRE(library.parent_path() == projects.parent_path());
    REQUIRE(library.parent_path().filename() == "Plynth");
}

TEST_CASE("thumbnail engine names", "[core][settings]") {
    REQUIRE(thumbnailEngineName(ThumbnailEngine::Eevee) == "eevee");
    REQUIRE(thumbnailEngineName(ThumbnailEngine::Cycles) == "cycles");
    REQUIRE(thumbnailEngineFromName("cycles") == ThumbnailEngine::Cycles);
    REQUIRE_FALSE(thumbnailEngineFromName("workbench"));
}
