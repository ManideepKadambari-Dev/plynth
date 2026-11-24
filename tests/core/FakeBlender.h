#pragma once

#include "TestSupport.h"

#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace plynth::test {

// Copies the fake Blender program to folder/blender.exe and writes its version and mode files next to it.
// Returns the path of blender.exe. An empty mode leaves fake_mode.txt out, so the environment decides.
inline std::filesystem::path makeFakeBlender(const std::filesystem::path& folder, const std::string& version,
                                             const std::string& mode = "")
{
    std::filesystem::create_directories(folder);
    const std::filesystem::path exe = folder / "blender.exe";
    std::filesystem::copy_file(PLYNTH_FAKE_BLENDER, exe, std::filesystem::copy_options::overwrite_existing);
    std::ofstream(folder / "fake_version.txt") << version;
    if (!mode.empty()) std::ofstream(folder / "fake_mode.txt") << mode;
    return exe;
}

// A folder labelled low that the fake Blender writes its records to while this object lives, through
// FAKE_BLENDER_RECORDS. A fetch runs the fake at low integrity, so its records need such a folder. The fake's own
// folder is never labelled: Windows would then start every run of the fake at low integrity.
struct FakeRecords {
    explicit FakeRecords(const std::filesystem::path& where) : folder(labelled(where)), variable("FAKE_BLENDER_RECORDS", pathToUtf8(folder)) {}

    std::filesystem::path folder;
    ScopedEnv variable;

private:
    // Makes the folder with the low label.
    static std::filesystem::path labelled(const std::filesystem::path& where)
    {
        requireOk(makeLowIntegrityFolder(where));
        return where;
    }
};

// A folder that holds plynth_worker.py, so runBlenderJob finds its scripts, and an empty fetch guard.
inline std::filesystem::path makeFakeScripts(const std::filesystem::path& folder)
{
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "plynth_worker.py") << "# stand in for the worker\n";
    std::filesystem::create_directories(folder / "guard" / "startup");
    std::ofstream(folder / "guard" / "startup" / "plynth_fetch_guard.py") << "# stand in for the guard\n";
    return folder;
}

// A made-up user for tests that start the fake Blender: APPDATA, USERPROFILE, the private Blender folder and
// the five BLENDER_USER_ variables and the temporary folder all point into one temporary folder, and FAKE_BLENDER_USER_ROOT points at
// the user folder, so a run that missed the private folder would show in the fingerprint. XDG_DATA_HOME is unset.
struct IsolatedUser {
    explicit IsolatedUser(const std::string& tag) : tmp(tag)
    {
        const std::filesystem::path root = tmp.path();
        userRoot = root / "user";
        for (const char* sub : {"user/config", "user/scripts", "user/extensions", "user/datafiles", "appdata", "profile", "private", "temp"}) {
            std::filesystem::create_directories(root / sub);
        }
        const auto set = [&](const char* name, const std::filesystem::path& value) {
            const std::u8string text = value.generic_u8string();
            envs.push_back(std::make_unique<ScopedEnv>(name, std::string(text.begin(), text.end())));
        };
        tempFolder = root / "temp";
        set("TMP", tempFolder);
        set("TEMP", tempFolder);
        set("APPDATA", root / "appdata");
        set("USERPROFILE", root / "profile");
        set("XDG_DATA_HOME", {});
        set("PLYNTH_BLENDER_PRIVATE", root / "private");
        set("BLENDER_USER_RESOURCES", userRoot);
        set("BLENDER_USER_CONFIG", userRoot / "config");
        set("BLENDER_USER_SCRIPTS", userRoot / "scripts");
        set("BLENDER_USER_EXTENSIONS", userRoot / "extensions");
        set("BLENDER_USER_DATAFILES", userRoot / "datafiles");
        set("FAKE_BLENDER_USER_ROOT", userRoot);
    }

    // Everything under the folders a real run could touch: relative path, size and write time of each entry.
    std::vector<std::string> fingerprint() const
    {
        std::vector<std::string> out;
        for (const char* sub : {"user", "appdata", "profile"}) {
            const std::filesystem::path base = tmp.path() / sub;
            out.push_back(std::string(sub) + "/");
            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec)) {
                const std::u8string rel = entry.path().lexically_relative(tmp.path()).generic_u8string();
                std::string line(rel.begin(), rel.end());
                if (entry.is_regular_file(ec)) {
                    line += "|" + std::to_string(entry.file_size(ec)) + "|" + std::to_string(entry.last_write_time(ec).time_since_epoch().count());
                }
                out.push_back(line);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    TempDir tmp;
    std::filesystem::path userRoot;
    std::filesystem::path tempFolder; // where temp_directory_path() points, so work folders can be counted
    std::vector<std::unique_ptr<ScopedEnv>> envs;
};

}
