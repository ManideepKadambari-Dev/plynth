#pragma once

#include "EditSession.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Machine settings: what belongs to this computer rather than to one project. One file per user,
// %APPDATA%\Plynth\settings.json, in the format of schema/settings.schema.json. When the environment
// variable PLYNTH_CONFIG_DIR is set, its folder is used instead, so tests and scripts never touch the
// real file. Paths are stored as UTF-8 with forward slashes.

inline constexpr std::string_view kSettingsFileName = "settings.json";
inline constexpr std::string_view kSettingsFormat = "plynth-settings";
// Version 2 adds "recent_projects". A version 1 file is read as having none and is written back as version 2.
// Version 3 drops thumbnail_engine: thumbnails use Eevee only.
// Version 4 adds "tour_done". A file of version 1 to 3 is read with it true, so an upgrade does not start the
// first run tour. A fresh default is false.
inline constexpr int kSettingsFormatVersion = 4;
inline constexpr std::string_view kConfigDirVariable = "PLYNTH_CONFIG_DIR";

// An import with more triangles than this gets a warning. Never refused: the target is Nanite.
inline constexpr long long kDefaultTriangleWarning = 500000;

// The most recent projects kept in the settings.
inline constexpr std::size_t kMaxRecentProjects = 30;

// A project the app opened or saved, for the home screen (DESIGN.md section 7.19e).
struct RecentProject {
    std::filesystem::path path; // the project folder
    std::string name;           // the project name when it was last opened
    SessionTime lastOpened{};

    bool operator==(const RecentProject&) const = default;
};

struct MachineSettings {
    std::optional<std::filesystem::path> library;  // the Library folder (DESIGN.md section 5)
    std::optional<std::filesystem::path> projects; // where new projects go and where the terminal starts
    std::optional<std::filesystem::path> blender;  // blender.exe picked by the user; empty means the preselected one
    bool keepImportedZips = false;                 // DESIGN.md section 5 rule 4
    long long triangleWarning = kDefaultTriangleWarning;
    // The Plynth version whose setup check last passed. Empty before the first pass. When it differs from
    // version(), the app opens the setup check once ("after an update", DESIGN.md section 7.19b).
    std::string setupCheckedVersion;
    // Most recent first, at most kMaxRecentProjects. Only the app adds to it (when it opens or saves a project).
    std::vector<RecentProject> recentProjects;
    // True once the first run tour was shown or skipped. The app starts it once, after the setup check closes.
    bool tourDone = false;

    bool operator==(const MachineSettings&) const = default;
};

// Adds a project to the top of the recent list, or moves it there when it is already listed (the same folder,
// ignoring case and a trailing separator), with the new name and time. The list is cut to kMaxRecentProjects.
void addRecentProject(MachineSettings& settings, const std::filesystem::path& folder, const std::string& name, SessionTime when);

// Takes a project off the recent list. False when it was not listed. Nothing on disk is touched.
bool removeRecentProject(MachineSettings& settings, const std::filesystem::path& folder);

// Takes off every entry whose folder no longer exists and returns how many went. Only runs when asked for.
std::size_t pruneRecentProjects(MachineSettings& settings);

// Where the app keeps the small picture of a project's plan for the home screen: a file under the settings
// folder, settingsFolder()/thumbnails/<16 hex digits>.png, named from the project folder's path (made
// absolute, tidy and lower case). Projects stay free of cache files and a read only project works. Only the
// path is made here; the app draws the picture, and a picture for a folder that is gone is just left unused.
std::filesystem::path recentThumbnailPath(const std::filesystem::path& projectFolder);

// The folder that holds settings.json: PLYNTH_CONFIG_DIR when set, otherwise %APPDATA%\Plynth.
std::filesystem::path settingsFolder();

// settingsFolder() / settings.json.
std::filesystem::path settingsFile();

// The suggested folders on first run: Documents\Plynth\Library and Documents\Plynth\Projects.
std::filesystem::path defaultLibraryFolder();
std::filesystem::path defaultProjectsFolder();

// The answer from reading settings.
struct SettingsLoad {
    Status status;
    MachineSettings settings; // defaults when the file is missing or bad
    bool exists = false;      // the file was there; false means this is the first run
};

// Reads settings from a file. A missing file is ok with exists false. BadFile (with a JSON pointer) when the
// file is not valid JSON, has the wrong format, an unknown field or a bad value; UnsupportedVersion when the
// version is newer. Unknown fields are refused, as for project files.
SettingsLoad loadSettings(const std::filesystem::path& file);

// The same with the check for the file's existence given, so a check that fails (an error code set) can be
// tested. A failed check is CannotRead and exists false, never a first run, so defaults cannot overwrite a
// file that is there but could not be looked at.
using FileExistsCheck = std::function<bool(const std::filesystem::path&, std::error_code&)>;
SettingsLoad loadSettings(const std::filesystem::path& file, const FileExistsCheck& exists);

// Reads settingsFile().
SettingsLoad loadSettings();

// Writes settings through a temporary file and a rename, creating the folder when needed. CannotWrite.
Status saveSettings(const MachineSettings& settings, const std::filesystem::path& file);

// Writes settingsFile().
Status saveSettings(const MachineSettings& settings);

// The file form, also printed by "plynth settings show". Unset paths are null.
Json settingsToJson(const MachineSettings& settings);

// Reads the file form. The same refusals as loadSettings, with where filled.
Status settingsFromJson(const Json& json, MachineSettings& out);

}
