#pragma once

#include "Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace plynth {

using Json = nlohmann::ordered_json;

// Machine settings: what belongs to this computer rather than to one project. One file per user,
// %APPDATA%\Plynth\settings.json, in the format of schema/settings.schema.json. When the environment
// variable PLYNTH_CONFIG_DIR is set, its folder is used instead, so tests and scripts never touch the
// real file. Paths are stored as UTF-8 with forward slashes.

inline constexpr std::string_view kSettingsFileName = "settings.json";
inline constexpr std::string_view kSettingsFormat = "plynth-settings";
inline constexpr int kSettingsFormatVersion = 1;
inline constexpr std::string_view kConfigDirVariable = "PLYNTH_CONFIG_DIR";

// An import with more triangles than this gets a warning. Never refused: the target is Nanite.
inline constexpr long long kDefaultTriangleWarning = 500000;

// How Blender renders thumbnails. Eevee is fast but needs a working graphics driver; when it fails the
// worker tries Cycles on the processor once, and the result says which engine was used.
enum class ThumbnailEngine { Eevee, Cycles };

// "eevee" or "cycles".
std::string_view thumbnailEngineName(ThumbnailEngine engine);

// The engine for a name used in files. Empty when the name is unknown.
std::optional<ThumbnailEngine> thumbnailEngineFromName(std::string_view name);

struct MachineSettings {
    std::optional<std::filesystem::path> library;  // the Library folder (DESIGN.md section 5)
    std::optional<std::filesystem::path> projects; // where new projects go and where the terminal starts
    std::optional<std::filesystem::path> blender;  // blender.exe picked by the user; empty means the preselected one
    bool keepImportedZips = false;                 // DESIGN.md section 5 rule 4
    ThumbnailEngine thumbnailEngine = ThumbnailEngine::Eevee;
    long long triangleWarning = kDefaultTriangleWarning;
    // The Plynth version whose setup check last passed. Empty before the first pass. When it differs from
    // version(), the app opens the setup check once ("after an update", DESIGN.md section 7.19b).
    std::string setupCheckedVersion;

    bool operator==(const MachineSettings&) const = default;
};

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
