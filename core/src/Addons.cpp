#include <plynth/core/Addons.h>

#include "BlenderPaths.h"
#include "FileIo.h"

#include <plynth/core/BlenderJob.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/SetupCheck.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Lower cases ASCII text.
std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Makes a refusal of the cache file.
AddonCacheRead refused(std::string message, std::string where = {})
{
    AddonCacheRead read;
    read.status = fail(ErrorCode::BadFile, std::move(message), std::move(where));
    return read;
}

// Reads a text field that must be present. Null when it is not text.
const std::string* textField(const Json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) return nullptr;
    return it->get_ptr<const std::string*>();
}

// Says whether this is an add-on cache saved by an older Plynth, which is never used.
bool isOlderCache(const Json& json)
{
    const std::string* format = textField(json, "format");
    if (!format || *format != kAddonCacheFormat) return false;
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer()) return false;
    const long long found = version->get<long long>();
    return found >= 1 && found < kAddonCacheVersion;
}

// Says whether a part of a module name is letters, digits and "_" only.
bool plainPart(std::string_view part)
{
    return !part.empty() && std::all_of(part.begin(), part.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// Splits bl_ext.<repository>.<id> into its two parts. False for any other form.
bool extensionParts(std::string_view module, std::string& repository, std::string& id)
{
    constexpr std::string_view prefix = "bl_ext.";
    if (!module.starts_with(prefix)) return false;
    const std::string_view rest = module.substr(prefix.size());
    const std::size_t dot = rest.find('.');
    if (dot == std::string_view::npos) return false;
    repository = std::string(rest.substr(0, dot));
    id = std::string(rest.substr(dot + 1));
    return plainPart(repository) && plainPart(id);
}

// Says whether attributes belong to a plain folder that exists, not a link or junction.
bool plainFolder(DWORD attributes)
{
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Says whether attributes belong to a plain file that exists, not a link.
bool plainFile(DWORD attributes)
{
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

// Says whether a folder name is Python's cache, which is never copied.
bool isCacheFolder(const std::wstring& name)
{
    return CompareStringOrdinal(name.c_str(), -1, L"__pycache__", -1, TRUE) == CSTR_EQUAL;
}

// One file or folder inside an add-on, relative to the add-on's folder.
struct AddonEntry {
    fs::path relative;
    bool folder = false;
};

// What looking through an add-on found.
enum class AddonScan { Ok, LeftOut, Unreadable, Stopped };

// Lists an add-on's folder and everything below it without going through any link. Python caches are looked
// through for links too, but their entries are not kept.
AddonScan scanAddon(const fs::path& root, const fs::path& relative, bool keep, std::vector<AddonEntry>& entries,
                    std::stop_token stop)
{
    if (stop.stop_requested()) return AddonScan::Stopped;
    const fs::path folder = relative.empty() ? root : root / relative;
    WIN32_FIND_DATAW found{};
    const HANDLE find = FindFirstFileExW(blenderpaths::longPath(folder / L"*").c_str(), FindExInfoBasic, &found,
                                         FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return AddonScan::Unreadable;
    AddonScan result = AddonScan::Ok;
    std::vector<std::pair<fs::path, bool>> inside;
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") continue;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            result = AddonScan::LeftOut;
            break;
        }
        const fs::path inner = relative.empty() ? fs::path(name) : relative / name;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            const bool keepInner = keep && !isCacheFolder(name);
            if (keepInner) entries.push_back({inner, true});
            inside.emplace_back(inner, keepInner);
        } else if (keep) {
            entries.push_back({inner, false});
        }
    } while (FindNextFileW(find, &found));
    const DWORD error = GetLastError();
    FindClose(find);
    if (result != AddonScan::Ok) return result;
    if (error != ERROR_NO_MORE_FILES) return AddonScan::Unreadable;
    for (const auto& [inner, keepInner] : inside) {
        if (const AddonScan scan = scanAddon(root, inner, keepInner, entries, stop); scan != AddonScan::Ok) return scan;
    }
    return AddonScan::Ok;
}

// Copies one file, opening the source for reading only. The copy is a plain file without the read-only mark.
Status copyAddonFile(const fs::path& from, const fs::path& to)
{
    const HANDLE in = CreateFileW(blenderpaths::longPath(from).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (in == INVALID_HANDLE_VALUE) return fail(ErrorCode::CannotRead, "The add-on file " + pathToUtf8(from) + " could not be read.");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(in, &info) || !plainFile(info.dwFileAttributes)) {
        CloseHandle(in);
        return fail(ErrorCode::CannotRead, "The add-on file " + pathToUtf8(from) + " changed while it was copied.");
    }
    const HANDLE out = CreateFileW(blenderpaths::longPath(to).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        CloseHandle(in);
        return fail(ErrorCode::CannotWrite, "The copy " + pathToUtf8(to) + " could not be written.");
    }
    Status status;
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(in, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
            status = fail(ErrorCode::CannotRead, "The add-on file " + pathToUtf8(from) + " could not be read.");
            break;
        }
        if (got == 0) break;
        DWORD written = 0;
        if (!WriteFile(out, buffer.data(), got, &written, nullptr) || written != got) {
            status = fail(ErrorCode::CannotWrite, "The copy " + pathToUtf8(to) + " could not be written.");
            break;
        }
    }
    CloseHandle(out);
    CloseHandle(in);
    return status;
}

// Makes one new folder. False when it is there already or cannot be made.
bool makeFolder(const fs::path& folder)
{
    return CreateDirectoryW(blenderpaths::longPath(folder).c_str(), nullptr) != 0;
}

// Checks the folder a job's copies go to, by the rules at the top of Addons.h, without making anything.
Status checkJobFolder(const fs::path& job, const BlenderUserFolders& folders)
{
    const auto refused = [&](const std::string& why) {
        return fail(ErrorCode::InvalidValue, "The add-on copies were not made in " + pathToUtf8(job) + ", because " + why);
    };
    if (job.empty() || !job.is_absolute()) return refused("it is not an absolute path.");
    if (blenderpaths::namesBlenderFoundation(job)) return refused("its path names the Blender Foundation folder.");
    const fs::path resolved = blenderpaths::resolvedPath(job);
    if (resolved.empty()) return refused("it could not be worked out.");
    if (blenderpaths::attributesOf(job) != INVALID_FILE_ATTRIBUTES || blenderpaths::attributesOf(resolved) != INVALID_FILE_ATTRIBUTES) {
        return refused("it is there already.");
    }
    std::error_code ec;
    std::vector<fs::path> kept{folders.extensions, folders.config,         folders.scripts,
                               folders.datafiles,  folders.blenderKitData, fs::absolute(blenderpaths::privateFolder(), ec)};
    if (const std::wstring app = blenderpaths::environmentText(L"APPDATA"); !app.empty()) {
        kept.push_back(fs::absolute(fs::path(app) / L"Blender Foundation", ec));
    }
    for (const fs::path& other : kept) {
        if (blenderpaths::overlaps(job, other)) return refused("it is in or around " + pathToUtf8(other) + ".");
    }
    return {};
}

}

// The short name of an add-on module.
std::string addonShortName(std::string_view module)
{
    const std::size_t dot = module.rfind('.');
    return lowered(std::string(dot == std::string_view::npos ? module : module.substr(dot + 1)));
}

// Says whether an add-on is in the enabled list.
bool addonEnabled(const AddonCache& cache, std::string_view shortName)
{
    const std::string wanted = lowered(std::string(shortName));
    return std::any_of(cache.enabled.begin(), cache.enabled.end(),
                       [&](const ProbeAddon& a) { return addonShortName(a.module) == wanted; });
}

// Says whether the cache was made by this Blender.
bool addonCacheMatches(const AddonCache& cache, const BlenderInstall& install)
{
    if (!install.version) return false;
    if (cache.blenderVersion != formatBlenderVersion(*install.version)) return false;
    return lowered(pathToUtf8(cache.blender)) == lowered(pathToUtf8(install.executable));
}

// The name to show for an add-on.
std::string addonDisplayName(std::string_view shortName)
{
    const std::string wanted = lowered(std::string(shortName));
    for (const KnownAddon& addon : knownAddons()) {
        for (const std::string_view module : addon.modules) {
            if (module == wanted) return std::string(addon.name);
        }
    }
    return std::string(shortName);
}

// Makes a cache from a probe of this Blender.
AddonCache addonCacheFromProbe(const BlenderInstall& install, const ProbeResult& probe, SessionTime now)
{
    AddonCache cache;
    cache.blender = install.executable;
    cache.blenderVersion = install.version ? formatBlenderVersion(*install.version) : probe.version;
    cache.checkedAt = now;
    cache.enabled = probe.addons;
    std::sort(cache.enabled.begin(), cache.enabled.end(),
              [](const ProbeAddon& a, const ProbeAddon& b) { return a.module < b.module; });
    return cache;
}

// Where the cache lives.
fs::path addonCacheFile()
{
    return settingsFolder() / std::string(kAddonCacheFileName);
}

// The cache in its file form.
Json addonCacheToJson(const AddonCache& cache)
{
    std::vector<ProbeAddon> sorted = cache.enabled;
    std::sort(sorted.begin(), sorted.end(), [](const ProbeAddon& a, const ProbeAddon& b) { return a.module < b.module; });
    Json enabled = Json::array();
    for (const ProbeAddon& a : sorted) {
        enabled.push_back(Json{{"module", a.module}, {"name", a.name}, {"version", a.version}});
    }
    Json json = Json::object();
    json["format"] = std::string(kAddonCacheFormat);
    json["version"] = kAddonCacheVersion;
    json["blender"] = pathToUtf8(cache.blender);
    json["blender_version"] = cache.blenderVersion;
    json["checked_at"] = formatSessionTime(cache.checkedAt);
    json["enabled"] = std::move(enabled);
    return json;
}

// Reads the cache file.
AddonCacheRead readAddonCache(const fs::path& file)
{
    std::error_code ec;
    if (!fs::exists(file, ec)) return {};
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        AddonCacheRead read;
        read.status = fail(ErrorCode::CannotRead, "The add-on cache " + pathToUtf8(file) + " could not be read.");
        return read;
    }
    std::ostringstream text;
    text << in.rdbuf();
    const Json json = Json::parse(text.str(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) return refused("The add-on cache is not valid JSON.");

    if (isOlderCache(json)) return {};
    for (const auto& [key, value] : json.items()) {
        if (key != "format" && key != "version" && key != "blender" && key != "blender_version" && key != "checked_at" &&
            key != "enabled") {
            return refused("The field \"" + key + "\" is not known.", "/" + key);
        }
    }
    const std::string* format = textField(json, "format");
    if (!format || *format != kAddonCacheFormat) return refused("The format must be \"plynth-addons\".", "/format");
    if (!json.contains("version") || !json["version"].is_number_integer() || json["version"].get<int>() != kAddonCacheVersion) {
        return refused("The version is not supported.", "/version");
    }
    const std::string* blender = textField(json, "blender");
    if (!blender || blender->empty()) return refused("The field \"blender\" must be a path.", "/blender");
    const std::string* blenderVersion = textField(json, "blender_version");
    if (!blenderVersion) return refused("The field \"blender_version\" must be text.", "/blender_version");
    const std::string* checked = textField(json, "checked_at");
    const std::optional<SessionTime> time = checked ? parseSessionTime(*checked) : std::nullopt;
    if (!time) return refused("The field \"checked_at\" must be a time such as 2026-11-22T10:15:00Z.", "/checked_at");
    if (!json.contains("enabled") || !json["enabled"].is_array()) return refused("The field \"enabled\" must be a list.", "/enabled");

    AddonCache cache;
    cache.blender = pathFromUtf8(*blender);
    cache.blenderVersion = *blenderVersion;
    cache.checkedAt = *time;
    const Json& list = json["enabled"];
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string where = "/enabled/" + std::to_string(i);
        if (!list[i].is_object()) return refused("An add-on entry must be an object.", where);
        for (const auto& [key, value] : list[i].items()) {
            if (key != "module" && key != "name" && key != "version") {
                return refused("The field \"" + key + "\" is not known.", where + "/" + key);
            }
        }
        const std::string* module = textField(list[i], "module");
        const std::string* name = textField(list[i], "name");
        const std::string* version = textField(list[i], "version");
        if (!module || module->empty()) return refused("The field \"module\" must be text.", where + "/module");
        if (!name) return refused("The field \"name\" must be text.", where + "/name");
        if (!version) return refused("The field \"version\" must be text.", where + "/version");
        cache.enabled.push_back({*module, *name, *version});
    }
    std::sort(cache.enabled.begin(), cache.enabled.end(),
              [](const ProbeAddon& a, const ProbeAddon& b) { return a.module < b.module; });
    AddonCacheRead read;
    read.cache = std::move(cache);
    return read;
}

// Writes the cache.
Status writeAddonCache(const fs::path& file, const AddonCache& cache)
{
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    return fileio::writeAtomic(file, addonCacheToJson(cache).dump(2) + "\n");
}

// Asks Blender which add-ons are enabled.
AddonProbe probeAddons(const BlenderInstall& blender, const fs::path& cacheFile, const BlenderRunOptions& run,
                       std::stop_token stop)
{
    AddonProbe probe;
    const BlenderProbeRun ran = runBlenderProbe(blender, run, stop);
    if (ran.status.code == ErrorCode::Cancelled) {
        probe.status = ran.status;
    } else if (!ran.status.ok()) {
        probe.status = fail(ErrorCode::BlenderFailed, "Blender could not list its add-ons: " + ran.status.message);
    } else {
        probe.cache = addonCacheFromProbe(blender, ran.result, sessionNow());
        probe.status = writeAddonCache(cacheFile, probe.cache);
    }
    return probe;
}

// Copies the needed add-ons into one job's own folder.
AddonFolderCopy prepareAddonFolder(const fs::path& jobFolder, const fs::path& source, const BlenderInstall& blender,
                                   const AddonCache& cache, const std::vector<std::string>& shortNames, std::stop_token stop)
{
    AddonFolderCopy out;
    const auto endWith = [&](Status status) {
        out.status = std::move(status);
        out.copied.clear();
        return out;
    };
    const auto refused = [&](const std::string& why) { return endWith(fail(ErrorCode::InvalidValue, why)); };
    const auto stopped = [&] { return endWith(fail(ErrorCode::Cancelled, "Copying the add-ons was stopped.")); };

    // The source is judged by its text alone before anything of it is looked at.
    const BlenderUserFolders folders = blenderUserFolders(blender);
    const fs::path& extensions = folders.extensions;
    if (source.empty() || !source.is_absolute() || extensions.empty() || !blenderpaths::sameText(source, extensions)) {
        return refused("Add-ons are copied only from the extensions folder of the Blender that builds.");
    }
    if (!addonCacheMatches(cache, blender)) {
        return refused("The list of enabled add-ons was made by another Blender, so none is copied.");
    }
    if (blenderpaths::hasLinkOnPath(extensions)) {
        return refused("The extensions folder " + pathToUtf8(extensions) +
                       " is reached through a link or junction, so no add-on is copied.");
    }
    for (const fs::path& other : {folders.config, folders.scripts, folders.datafiles, folders.blenderKitData}) {
        if (blenderpaths::overlaps(extensions, other)) {
            return refused("The extensions folder " + pathToUtf8(extensions) + " shares a folder with " + pathToUtf8(other) +
                           ", so no add-on is copied.");
        }
    }
    if (!plainFolder(blenderpaths::attributesOf(extensions))) {
        return endWith(fail(ErrorCode::CannotRead, "The extensions folder " + pathToUtf8(extensions) + " was not found."));
    }
    if (Status s = checkJobFolder(jobFolder, folders); !s.ok()) return endWith(s);
    if (stop.stop_requested()) return stopped();

    const fs::path job = blenderpaths::normalFolder(jobFolder);
    std::error_code ec;
    fs::create_directories(job.parent_path(), ec);
    if (ec) return endWith(fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(job.parent_path()) + " could not be made."));
    if (!makeFolder(job)) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) return refused("The folder " + pathToUtf8(job) + " is there already.");
        return endWith(fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(job) + " could not be made."));
    }
    const fs::path into = job / L"user_default";
    if (!makeFolder(into)) return endWith(fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(into) + " could not be made."));

    std::vector<std::string> wanted;
    for (const std::string& name : shortNames) wanted.push_back(lowered(name));
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    const fs::path resolvedExtensions = blenderpaths::resolvedPath(extensions);

    for (const std::string& name : wanted) {
        if (stop.stop_requested()) return stopped();
        std::string repository;
        std::string id;
        bool listed = false;
        for (const ProbeAddon& addon : cache.enabled) {
            std::string r;
            std::string i;
            if (!extensionParts(addon.module, r, i) || lowered(i) != name) continue;
            repository = std::move(r);
            id = std::move(i);
            listed = true;
            break;
        }
        if (!listed) continue;

        // Reached by name, so neither the extensions folder nor the repository is ever listed.
        const fs::path repositoryFolder = extensions / pathFromUtf8(repository);
        const fs::path addonFolder = repositoryFolder / pathFromUtf8(id);
        if (!plainFolder(blenderpaths::attributesOf(repositoryFolder)) || !plainFolder(blenderpaths::attributesOf(addonFolder))) {
            continue;
        }
        if (resolvedExtensions.empty() ||
            !blenderpaths::sameText(blenderpaths::resolvedPath(addonFolder),
                                    resolvedExtensions / pathFromUtf8(repository) / pathFromUtf8(id))) {
            continue;
        }
        if (!plainFile(blenderpaths::attributesOf(addonFolder / L"blender_manifest.toml"))) continue;

        std::vector<AddonEntry> entries;
        const AddonScan scan = scanAddon(addonFolder, {}, true, entries, stop);
        if (scan == AddonScan::Stopped) return stopped();
        if (scan == AddonScan::LeftOut) continue;
        if (scan == AddonScan::Unreadable) {
            return endWith(fail(ErrorCode::CannotRead, "The add-on folder " + pathToUtf8(addonFolder) + " could not be read."));
        }

        const fs::path copy = into / pathFromUtf8(id);
        if (!makeFolder(copy)) return endWith(fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(copy) + " could not be made."));
        for (const AddonEntry& entry : entries) {
            if (stop.stop_requested()) return stopped();
            if (entry.folder) {
                if (!makeFolder(copy / entry.relative)) {
                    return endWith(fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(copy / entry.relative) + " could not be made."));
                }
            } else if (Status s = copyAddonFile(addonFolder / entry.relative, copy / entry.relative); !s.ok()) {
                return endWith(s);
            }
        }
        out.copied.push_back(name);
    }

    if (stop.stop_requested()) return stopped();
    const HANDLE marker = CreateFileW(blenderpaths::longPath(job / pathFromUtf8(kPreparedExtensionsMarker)).c_str(), GENERIC_WRITE,
                                      0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (marker == INVALID_HANDLE_VALUE) {
        return endWith(fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(job) + " could not be marked as ready."));
    }
    CloseHandle(marker);
    std::sort(out.copied.begin(), out.copied.end());
    return out;
}

}
