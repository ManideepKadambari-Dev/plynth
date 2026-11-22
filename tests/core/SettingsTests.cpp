#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <process.h>

#include <chrono>
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
    SECTION("version 4 is unsupported_version") {
        const SettingsLoad l = loadSettings(dataFile("settings-version-4.json"));
        REQUIRE(l.status.code == ErrorCode::UnsupportedVersion);
    }
    SECTION("a version 3 file may not carry the engine") {
        const SettingsLoad l = loadSettings(dataFile("settings-v3-with-engine.json"));
        REQUIRE(l.status.code == ErrorCode::BadFile);
        REQUIRE(l.status.where == "/thumbnail_engine");
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
    REQUIRE(writeAndLoad(R"({"format":"other","version":2})").status.where == "/format");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings"})").status.where == "/version");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":2,"library":""})").status.where == "/library");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":2,"triangle_warning":0})").status.where ==
            "/triangle_warning");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":2,"keep_imported_zips":"yes"})").status.where ==
            "/keep_imported_zips");
    REQUIRE(writeAndLoad(R"({"format":"plynth-settings","version":2,"setup_checked_version":3})").status.where ==
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
    s.setupCheckedVersion = "0.6.0";
    REQUIRE(plynth::test::schemaProblem("settings.schema.json", plain(settingsToJson(s))).empty());

    for (const char* name : {"settings-valid.json", "settings-minimal.json"}) {
        INFO(name);
        const auto json = nlohmann::json::parse(plynth::test::readTextFile(dataFile(name)));
        REQUIRE(plynth::test::schemaProblem("settings.schema.json", json).empty());
    }
    for (const char* name : {"settings-unknown-field.json", "settings-version-4.json", "settings-bad-engine.json",
                                  "settings-v3-with-engine.json", "settings-v2.json"}) {
        INFO(name);
        const auto json = nlohmann::json::parse(plynth::test::readTextFile(dataFile(name)));
        REQUIRE_FALSE(plynth::test::schemaProblem("settings.schema.json", json).empty());
    }
}

TEST_CASE("a version 1 settings file is read and written back as version 3", "[core][settings][migrate]") {
    plynth::test::TempDir tmp("settings-v1");
    const SettingsLoad l = loadSettings(dataFile("settings-v1.json"));
    plynth::test::requireOk(l.status);
    REQUIRE(l.settings.library == fs::path("D:/Lib"));
    REQUIRE(l.settings.triangleWarning == 1000);
    REQUIRE(l.settings.recentProjects.empty());

    const auto plain = [](const Json& j) { return nlohmann::json::parse(j.dump()); };
    const auto v1 = nlohmann::json::parse(plynth::test::readTextFile(dataFile("settings-v1.json")));
    REQUIRE(plynth::test::schemaProblem("settings.v1.schema.json", v1).empty());
    REQUIRE_FALSE(plynth::test::schemaProblem("settings.schema.json", v1).empty());

    const fs::path file = tmp.path() / "settings.json";
    plynth::test::requireOk(saveSettings(l.settings, file));
    const std::string text = plynth::test::readTextFile(file);
    REQUIRE(text.find("\"version\": 3") != std::string::npos);
    REQUIRE(text.find("\"recent_projects\": []") != std::string::npos);
    REQUIRE(plynth::test::schemaProblem("settings.schema.json", plain(settingsToJson(l.settings))).empty());

    const SettingsLoad bad = [&] {
        std::ofstream(file, std::ios::binary) << R"({"format":"plynth-settings","version":1,"recent_projects":[]})";
        return loadSettings(file);
    }();
    REQUIRE(bad.status.code == ErrorCode::BadFile);
}

TEST_CASE("recent projects are listed newest first without repeats", "[core][settings][recent]") {
    MachineSettings s;
    const SessionTime t0 = sessionNow();
    addRecentProject(s, "D:/Projects/A", "A", t0);
    addRecentProject(s, "D:/Projects/B", "B", t0 + std::chrono::seconds(10));
    REQUIRE(s.recentProjects.size() == 2);
    REQUIRE(s.recentProjects[0].name == "B");

    addRecentProject(s, "d:/projects/a/", "A renamed", t0 + std::chrono::seconds(20));
    REQUIRE(s.recentProjects.size() == 2);
    REQUIRE(s.recentProjects[0].name == "A renamed");
    REQUIRE(s.recentProjects[0].lastOpened == t0 + std::chrono::seconds(20));
    REQUIRE(s.recentProjects[1].name == "B");

    REQUIRE_FALSE(removeRecentProject(s, "D:/Projects/Z"));
    REQUIRE(removeRecentProject(s, "D:/PROJECTS/B"));
    REQUIRE(s.recentProjects.size() == 1);
}

TEST_CASE("the recent list is capped at 30", "[core][settings][recent]") {
    MachineSettings s;
    const SessionTime t0 = sessionNow();
    for (int i = 0; i < 35; ++i) {
        addRecentProject(s, "D:/Projects/P" + std::to_string(i), "P" + std::to_string(i), t0 + std::chrono::seconds(i));
    }
    REQUIRE(s.recentProjects.size() == kMaxRecentProjects);
    REQUIRE(s.recentProjects.front().name == "P34");
    REQUIRE(s.recentProjects.back().name == "P5");
}

TEST_CASE("prune takes off only entries whose folder is gone", "[core][settings][recent]") {
    plynth::test::TempDir tmp("settings-prune");
    fs::create_directories(tmp.path() / "here");
    MachineSettings s;
    addRecentProject(s, tmp.path() / "gone", "Gone", sessionNow());
    addRecentProject(s, tmp.path() / "here", "Here", sessionNow());
    REQUIRE(s.recentProjects.size() == 2);
    REQUIRE(pruneRecentProjects(s) == 1);
    REQUIRE(s.recentProjects.size() == 1);
    REQUIRE(s.recentProjects[0].name == "Here");
    REQUIRE(pruneRecentProjects(s) == 0);
}

TEST_CASE("recent projects round trip and validate", "[core][settings][recent][schema]") {
    plynth::test::TempDir tmp("settings-recent");
    MachineSettings s;
    addRecentProject(s, "D:/Projects/Flat ü", "Flat ü", sessionNow());
    addRecentProject(s, "D:/Projects/House", "House", sessionNow());
    const fs::path file = tmp.path() / "settings.json";
    plynth::test::requireOk(saveSettings(s, file));
    const SettingsLoad loaded = loadSettings(file);
    plynth::test::requireOk(loaded.status);
    REQUIRE(loaded.settings == s);
    const auto json = nlohmann::json::parse(settingsToJson(s).dump());
    REQUIRE(plynth::test::schemaProblem("settings.schema.json", json).empty());

    const auto bad = [&](const std::string& entry) {
        std::ofstream(file, std::ios::binary) << R"({"format":"plynth-settings","version":2,"recent_projects":[)" + entry + "]}";
        return loadSettings(file).status;
    };
    REQUIRE(bad(R"({"path":"","name":"A","last_opened":"2026-01-01T00:00:00Z"})").where == "/recent_projects/0/path");
    REQUIRE(bad(R"({"path":"D:/A","name":5,"last_opened":"2026-01-01T00:00:00Z"})").where == "/recent_projects/0/name");
    REQUIRE(bad(R"({"path":"D:/A","name":"A","last_opened":"yesterday"})").where == "/recent_projects/0/last_opened");
    REQUIRE(bad(R"({"path":"D:/A","name":"A","last_opened":"2026-01-01T00:00:00Z","x":1})").where == "/recent_projects/0/x");
}

TEST_CASE("the thumbnail path is stable, per folder and under the settings folder", "[core][settings][recent]") {
    plynth::test::TempDir tmp("settings-thumb");
    plynth::test::ScopedEnv env("PLYNTH_CONFIG_DIR", plynth::pathToUtf8(tmp.path() / "config"));
    const fs::path a = recentThumbnailPath("D:/Projects/Flat");
    REQUIRE(a == recentThumbnailPath("d:/projects/flat/"));
    REQUIRE(a != recentThumbnailPath("D:/Projects/House"));
    REQUIRE(a.parent_path() == tmp.path() / "config" / "thumbnails");
    REQUIRE(a.extension() == ".png");
    REQUIRE(a.stem().string().size() == 16);
}

TEST_CASE("PLYNTH_CONFIG_DIR changes the settings file", "[core][settings]") {
    // The variable is set in the ANSI code page, so this folder name stays plain ASCII.
    const fs::path base = fs::temp_directory_path() / ("plynth-settings-env-" + std::to_string(_getpid()));
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

TEST_CASE("a version 2 settings file with an engine is read and saved as version 3", "[core][settings][migrate]") {
    plynth::test::TempDir tmp("settings-v2");
    const SettingsLoad l = loadSettings(dataFile("settings-v2.json"));
    plynth::test::requireOk(l.status);
    REQUIRE(l.settings.library == fs::path("D:/Lib"));
    REQUIRE(l.settings.keepImportedZips);
    REQUIRE(l.settings.triangleWarning == 1000);
    REQUIRE(l.settings.recentProjects.size() == 1);

    const auto v2 = nlohmann::json::parse(plynth::test::readTextFile(dataFile("settings-v2.json")));
    REQUIRE(plynth::test::schemaProblem("settings.v2.schema.json", v2).empty());
    REQUIRE_FALSE(plynth::test::schemaProblem("settings.schema.json", v2).empty());

    const fs::path file = tmp.path() / "settings.json";
    plynth::test::requireOk(saveSettings(l.settings, file));
    const std::string text = plynth::test::readTextFile(file);
    REQUIRE(text.find("\"version\": 3") != std::string::npos);
    REQUIRE(text.find("thumbnail_engine") == std::string::npos);
    const auto saved = nlohmann::json::parse(text);
    REQUIRE(plynth::test::schemaProblem("settings.schema.json", saved).empty());
}
