#include <plynth/core/Addons.h>

#include "FileIo.h"

#include <plynth/core/BlenderJob.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/SetupCheck.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

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

}
