#pragma once

#include "Blender.h"
#include "BlenderJob.h"
#include "EditSession.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Which Blender add-ons are enabled, remembered between runs so the generator list can say which generators
// are available without starting Blender each time. Asking Blender (the probe task, BlenderJob.h) takes a few
// seconds; the answer is kept in one machine file next to the settings, schema/addon-cache.schema.json:
//   {"format": "plynth-addons", "version": 1, "blender": "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe",
//    "blender_version": "5.2.0", "checked_at": "2026-11-22T10:15:00Z",
//    "enabled": [{"module": "bl_ext.blender_org.archimesh", "name": "Archimesh", "version": "1.2.5"}]}
// It is a cache, not a setting: deleting it only means "not checked yet". The app probes in the background when
// the cache is missing or names another Blender; "plynth generator check-addons" probes on request.
//
// Add-ons are named by their short module name: the last dotted part of the module, lower case, so
// "bl_ext.blender_org.Bagapie" is "bagapie". Generators list the short names they need.

inline constexpr std::string_view kAddonCacheFileName = "addons.json";
inline constexpr std::string_view kAddonCacheFormat = "plynth-addons";
inline constexpr int kAddonCacheVersion = 1;
inline constexpr std::chrono::seconds kAddonProbeTimeout{120};

struct AddonCache {
    std::filesystem::path blender;   // the executable that was asked
    std::string blenderVersion;      // for example "5.2.0"
    SessionTime checkedAt{};
    std::vector<ProbeAddon> enabled; // enabled add-ons and extensions, sorted by module
};

// The short name of an add-on module: its last dotted part, lower case.
std::string addonShortName(std::string_view module);

// Says whether an add-on with this short name (any case) is in the cache's enabled list.
bool addonEnabled(const AddonCache& cache, std::string_view shortName);

// Says whether the cache was made by this Blender: the same executable and the same version.
bool addonCacheMatches(const AddonCache& cache, const BlenderInstall& install);

// The name to show for an add-on: the name in knownAddons() (SetupCheck.h) when one of its modules has this
// short name, for example "Home Builder", otherwise the short name itself.
std::string addonDisplayName(std::string_view shortName);

// Makes a cache from a probe of this Blender.
AddonCache addonCacheFromProbe(const BlenderInstall& install, const ProbeResult& probe, SessionTime now);

// settingsFolder() / kAddonCacheFileName.
std::filesystem::path addonCacheFile();

// The cache in its file form, as above.
Json addonCacheToJson(const AddonCache& cache);

// The answer from reading the cache file. A missing file is ok with no cache.
struct AddonCacheRead {
    Status status; // CannotRead, or BadFile when it is not valid JSON or breaks the schema's rules
    std::optional<AddonCache> cache;
};

AddonCacheRead readAddonCache(const std::filesystem::path& file);

// Writes the cache through a temporary file and a rename. CannotWrite when that fails.
Status writeAddonCache(const std::filesystem::path& file, const AddonCache& cache);

// The answer from asking Blender.
struct AddonProbe {
    Status status; // BlenderFailed when the probe did not run or its result was bad; Cancelled when stopped
    AddonCache cache;
};

// Runs the probe task in this Blender without the factory startup (so the user's add-ons are seen), in a fresh
// temporary work folder that is removed afterwards, and writes the cache file when it succeeds. run.factoryStartup
// is ignored. Blocks; the app runs it on a worker thread.
AddonProbe probeAddons(const BlenderInstall& blender, const std::filesystem::path& cacheFile,
                       const BlenderRunOptions& run = {false, kAddonProbeTimeout, {}}, std::stop_token stop = {});

}
