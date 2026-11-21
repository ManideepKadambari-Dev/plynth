#include "plynth/core/Settings.h"

#include "FileIo.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The value of an environment variable as a path. Empty when it is not set or empty.
fs::path pathFromEnvironment(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(got);
    return fs::path(value);
}

// A Windows known folder such as Documents. Empty when Windows does not know it.
fs::path knownFolder(const KNOWNFOLDERID& id)
{
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) result = fs::path(raw);
    CoTaskMemFree(raw);
    return result;
}

// Reads a path field that may be null. False and a status when it is not a non empty string.
bool readOptionalPath(const Json& json, const char* key, std::optional<fs::path>& out, Status& status)
{
    const auto it = json.find(key);
    if (it == json.end() || it->is_null()) {
        out.reset();
        return true;
    }
    if (!it->is_string() || it->get<std::string>().empty()) {
        status = fail(ErrorCode::BadFile, std::string("\"") + key + "\" must be a path or null.", std::string("/") + key);
        return false;
    }
    out = pathFromUtf8(it->get<std::string>());
    return true;
}

// The key that says two paths name the same project folder: absolute, tidy, no trailing separator, lower case.
std::string folderKey(const fs::path& folder)
{
    std::error_code ec;
    fs::path result = fs::absolute(folder, ec);
    if (ec) result = folder;
    result = result.lexically_normal();
    if (!result.has_filename() && result.has_parent_path() && result != result.root_path()) result = result.parent_path();
    std::string text = pathToUtf8(result);
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

// Reads the whole text of a file. False when it cannot be opened.
bool readText(const fs::path& file, std::string& out)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

}

// The folder that holds settings.json.
fs::path settingsFolder()
{
    if (fs::path dir = pathFromEnvironment(L"PLYNTH_CONFIG_DIR"); !dir.empty()) return dir;
    fs::path roaming = knownFolder(FOLDERID_RoamingAppData);
    if (roaming.empty()) roaming = pathFromEnvironment(L"APPDATA");
    return roaming / "Plynth";
}

// The settings file.
fs::path settingsFile()
{
    return settingsFolder() / pathFromUtf8(kSettingsFileName);
}

// Documents\Plynth\Library.
fs::path defaultLibraryFolder()
{
    fs::path docs = knownFolder(FOLDERID_Documents);
    if (docs.empty()) docs = pathFromEnvironment(L"USERPROFILE") / "Documents";
    return docs / "Plynth" / "Library";
}

// Documents\Plynth\Projects.
fs::path defaultProjectsFolder()
{
    return defaultLibraryFolder().parent_path() / "Projects";
}

// Puts a project at the top of the recent list.
void addRecentProject(MachineSettings& settings, const fs::path& folder, const std::string& name, SessionTime when)
{
    const std::string key = folderKey(folder);
    std::erase_if(settings.recentProjects, [&](const RecentProject& r) { return folderKey(r.path) == key; });
    settings.recentProjects.insert(settings.recentProjects.begin(), RecentProject{folder, name, when});
    if (settings.recentProjects.size() > kMaxRecentProjects) settings.recentProjects.resize(kMaxRecentProjects);
}

// Takes a project off the recent list.
bool removeRecentProject(MachineSettings& settings, const fs::path& folder)
{
    const std::string key = folderKey(folder);
    return std::erase_if(settings.recentProjects, [&](const RecentProject& r) { return folderKey(r.path) == key; }) > 0;
}

// Takes off the entries whose folder is gone.
std::size_t pruneRecentProjects(MachineSettings& settings)
{
    return std::erase_if(settings.recentProjects, [](const RecentProject& r) {
        std::error_code ec;
        return !fs::is_directory(r.path, ec);
    });
}

// The path of the home screen picture of a project.
fs::path recentThumbnailPath(const fs::path& projectFolder)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const char c : folderKey(projectFolder)) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    char name[24];
    std::snprintf(name, sizeof name, "%016llx.png", static_cast<unsigned long long>(hash));
    return settingsFolder() / "thumbnails" / name;
}

// The file form of the settings.
Json settingsToJson(const MachineSettings& settings)
{
    Json json = Json::object();
    json["format"] = std::string(kSettingsFormat);
    json["version"] = kSettingsFormatVersion;
    json["library"] = settings.library ? Json(pathToUtf8(*settings.library)) : Json(nullptr);
    json["projects"] = settings.projects ? Json(pathToUtf8(*settings.projects)) : Json(nullptr);
    json["blender"] = settings.blender ? Json(pathToUtf8(*settings.blender)) : Json(nullptr);
    json["keep_imported_zips"] = settings.keepImportedZips;
    json["triangle_warning"] = settings.triangleWarning;
    json["setup_checked_version"] =
        settings.setupCheckedVersion.empty() ? Json(nullptr) : Json(settings.setupCheckedVersion);
    Json recent = Json::array();
    for (const RecentProject& r : settings.recentProjects) {
        Json entry = Json::object();
        entry["path"] = pathToUtf8(r.path);
        entry["name"] = r.name;
        entry["last_opened"] = formatSessionTime(r.lastOpened);
        recent.push_back(std::move(entry));
    }
    json["recent_projects"] = std::move(recent);
    return json;
}

// Reads the file form of the settings.
Status settingsFromJson(const Json& json, MachineSettings& out)
{
    if (!json.is_object()) return fail(ErrorCode::BadFile, "The settings must be a JSON object.", "");

    const auto format = json.find("format");
    if (format == json.end() || !format->is_string() || format->get<std::string>() != kSettingsFormat) {
        return fail(ErrorCode::BadFile, "The file is not a Plynth settings file.", "/format");
    }
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer()) {
        return fail(ErrorCode::BadFile, "The settings file has no version number.", "/version");
    }
    const long long fileVersion = version->get<long long>();
    if (fileVersion > kSettingsFormatVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    "The settings file was written by a newer Plynth (version " + std::to_string(fileVersion) + ").",
                    "/version");
    }
    if (fileVersion < 1) {
        return fail(ErrorCode::BadFile, "The settings file version is not valid.", "/version");
    }

    static const std::set<std::string> known{"format", "version", "library", "projects", "blender",
                                             "keep_imported_zips", "triangle_warning",
                                             "setup_checked_version", "recent_projects"};
    for (const auto& [key, value] : json.items()) {
        if (!known.count(key) && !(key == "thumbnail_engine" && fileVersion < 3)) {
            return fail(ErrorCode::BadFile, "Unknown field \"" + key + "\".", "/" + key);
        }
    }
    // Older files may carry the engine; it is checked and then dropped.
    if (const auto it = json.find("thumbnail_engine"); it != json.end()) {
        const bool valid = it->is_string() && (it->get<std::string>() == "eevee" || it->get<std::string>() == "cycles");
        if (!valid) {
            return fail(ErrorCode::BadFile, "\"thumbnail_engine\" must be \"eevee\" or \"cycles\".", "/thumbnail_engine");
        }
    }

    MachineSettings result;
    Status status;
    if (!readOptionalPath(json, "library", result.library, status)) return status;
    if (!readOptionalPath(json, "projects", result.projects, status)) return status;
    if (!readOptionalPath(json, "blender", result.blender, status)) return status;

    if (const auto it = json.find("keep_imported_zips"); it != json.end()) {
        if (!it->is_boolean()) {
            return fail(ErrorCode::BadFile, "\"keep_imported_zips\" must be true or false.", "/keep_imported_zips");
        }
        result.keepImportedZips = it->get<bool>();
    }
    if (const auto it = json.find("triangle_warning"); it != json.end()) {
        if (!it->is_number_integer() || it->get<long long>() < 1) {
            return fail(ErrorCode::BadFile, "\"triangle_warning\" must be a whole number of at least 1.",
                        "/triangle_warning");
        }
        result.triangleWarning = it->get<long long>();
    }
    if (const auto it = json.find("setup_checked_version"); it != json.end() && !it->is_null()) {
        if (!it->is_string()) {
            return fail(ErrorCode::BadFile, "\"setup_checked_version\" must be text or null.", "/setup_checked_version");
        }
        result.setupCheckedVersion = it->get<std::string>();
    }
    if (const auto it = json.find("recent_projects"); it != json.end()) {
        if (fileVersion < 2) return fail(ErrorCode::BadFile, "Unknown field \"recent_projects\".", "/recent_projects");
        if (!it->is_array() || it->size() > kMaxRecentProjects) {
            return fail(ErrorCode::BadFile, "\"recent_projects\" must be a list of at most 30 projects.", "/recent_projects");
        }
        for (std::size_t i = 0; i < it->size(); ++i) {
            const Json& entry = (*it)[i];
            const std::string at = "/recent_projects/" + std::to_string(i);
            if (!entry.is_object()) return fail(ErrorCode::BadFile, "A recent project must be an object.", at);
            for (const auto& [key, value] : entry.items()) {
                if (key != "path" && key != "name" && key != "last_opened") {
                    return fail(ErrorCode::BadFile, "Unknown field \"" + key + "\".", at + "/" + key);
                }
            }
            const auto path = entry.find("path");
            if (path == entry.end() || !path->is_string() || path->get<std::string>().empty()) {
                return fail(ErrorCode::BadFile, "\"path\" must be a folder path.", at + "/path");
            }
            const auto name = entry.find("name");
            if (name == entry.end() || !name->is_string()) return fail(ErrorCode::BadFile, "\"name\" must be text.", at + "/name");
            const auto opened = entry.find("last_opened");
            const auto time = opened != entry.end() && opened->is_string() ? parseSessionTime(opened->get<std::string>()) : std::nullopt;
            if (!time) return fail(ErrorCode::BadFile, "\"last_opened\" must be a time like 2026-01-31T12:00:00Z.", at + "/last_opened");
            result.recentProjects.push_back({pathFromUtf8(path->get<std::string>()), name->get<std::string>(), *time});
        }
    }
    out = std::move(result);
    return {};
}

// Reads settings from a file.
SettingsLoad loadSettings(const fs::path& file)
{
    SettingsLoad result;
    std::error_code ec;
    if (!fs::exists(file, ec)) return result;

    std::string text;
    if (!readText(file, text)) {
        result.status = fail(ErrorCode::CannotRead, "The settings file " + pathToUtf8(file) + " could not be opened.");
        return result;
    }
    result.exists = true;
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded()) {
        result.status = fail(ErrorCode::BadFile, "The settings file " + pathToUtf8(file) + " is not valid JSON.");
        return result;
    }
    MachineSettings settings;
    result.status = settingsFromJson(json, settings);
    if (result.status.ok()) result.settings = std::move(settings);
    return result;
}

// Reads settingsFile().
SettingsLoad loadSettings()
{
    return loadSettings(settingsFile());
}

// Writes settings through a temporary file and a rename.
Status saveSettings(const MachineSettings& settings, const fs::path& file)
{
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    if (ec) return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(file.parent_path()) + " could not be created.");
    const std::string text = settingsToJson(settings).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    return fileio::writeAtomic(file, text);
}

// Writes settingsFile().
Status saveSettings(const MachineSettings& settings)
{
    return saveSettings(settings, settingsFile());
}

}
