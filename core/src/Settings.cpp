#include "plynth/core/Settings.h"

#include "FileIo.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

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

// The name used in files.
std::string_view thumbnailEngineName(ThumbnailEngine engine)
{
    return engine == ThumbnailEngine::Cycles ? "cycles" : "eevee";
}

// The engine for a name used in files.
std::optional<ThumbnailEngine> thumbnailEngineFromName(std::string_view name)
{
    if (name == "eevee") return ThumbnailEngine::Eevee;
    if (name == "cycles") return ThumbnailEngine::Cycles;
    return std::nullopt;
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
    json["thumbnail_engine"] = std::string(thumbnailEngineName(settings.thumbnailEngine));
    json["triangle_warning"] = settings.triangleWarning;
    json["setup_checked_version"] =
        settings.setupCheckedVersion.empty() ? Json(nullptr) : Json(settings.setupCheckedVersion);
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
    if (fileVersion < kSettingsFormatVersion) {
        return fail(ErrorCode::BadFile, "The settings file version is not valid.", "/version");
    }

    static const std::set<std::string> known{"format", "version", "library", "projects", "blender",
                                             "keep_imported_zips", "thumbnail_engine", "triangle_warning",
                                             "setup_checked_version"};
    for (const auto& [key, value] : json.items()) {
        if (!known.count(key)) return fail(ErrorCode::BadFile, "Unknown field \"" + key + "\".", "/" + key);
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
    if (const auto it = json.find("thumbnail_engine"); it != json.end()) {
        const auto engine = it->is_string() ? thumbnailEngineFromName(it->get<std::string>()) : std::nullopt;
        if (!engine) {
            return fail(ErrorCode::BadFile, "\"thumbnail_engine\" must be \"eevee\" or \"cycles\".", "/thumbnail_engine");
        }
        result.thumbnailEngine = *engine;
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
