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
//   {"format": "plynth-addons", "version": 2, "blender": "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe",
//    "blender_version": "5.2.0", "checked_at": "2026-11-22T10:15:00Z",
//    "enabled": [{"module": "bl_ext.blender_org.archimesh", "name": "Archimesh", "version": "1.2.5"}]}
// It is a cache, not a setting: deleting it only means "not checked yet". The app probes in the background when
// the cache is missing or names another Blender; "plynth generator check-addons" probes on request.
// Version 1 caches came from a probe that listed the add-ons Blender had loaded. Version 2 lists those saved as
// enabled and found on disk, so a version 1 cache is never used.
//
// Add-ons are named by their short module name: the last dotted part of the module, lower case, so
// "bl_ext.blender_org.Bagapie" is "bagapie". Generators list the short names they need.
//
// A job that switches add-ons on gets an extensions folder of its own (prepareAddonFolder), filled only from a
// source folder the caller names, laid out like an extensions folder. The user's own Blender folders are never
// a source:
//   - the source is refused with InvalidValue, before it is opened or listed, when it is empty or not absolute,
//     or is, lies in or holds %APPDATA%/Blender Foundation or a folder of blenderUserFolders; links in the
//     source path are then resolved and it is checked again
//   - the job's folder must be absolute, must not exist and must not lie in the source; only then is it made
//   - for each short name, the first cache module of the form bl_ext.<repository>.<id> with that short name
//     (parts of letters, digits and "_" only) is copied from <source>/<repository>/<id>, which needs a
//     blender_manifest.toml, to <job folder>/user_default/<id>, without __pycache__
//   - an add-on that is not found or holds a link is left out, which is not an error
//   - nothing else is listed or copied, nothing is written in the source, and kPreparedExtensionsMarker is
//     written last, only when every copy finished
//   - the caller removes a job folder that was made, also after a failure

inline constexpr std::string_view kAddonCacheFileName = "addons.json";
inline constexpr std::string_view kAddonCacheFormat = "plynth-addons";
inline constexpr int kAddonCacheVersion = 2;
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

// The answer from reading the cache file. A missing file and one of an older version are ok with no cache; an
// older one is replaced by the next probe.
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

// Asks this Blender with runBlenderProbe (BlenderJob.h): factory startup in Plynth's private folder, reading the
// user's saved settings as data. Writes the cache file only when that succeeds and never asks again with normal
// startup. run.factoryStartup is ignored. Blocks; the app runs it on a worker thread.
AddonProbe probeAddons(const BlenderInstall& blender, const std::filesystem::path& cacheFile,
                       const BlenderRunOptions& run = {true, kAddonProbeTimeout, {}}, std::stop_token stop = {});

// What copying add-ons for one job did.
struct AddonFolderCopy {
    Status status;                   // InvalidValue, CannotRead, CannotWrite or Cancelled; there is then no marker
    std::vector<std::string> copied; // short names, sorted
};

// Prepares one job's extensions folder from a source folder, by the rules at the top of this file.
AddonFolderCopy prepareAddonFolder(const std::filesystem::path& jobFolder, const std::filesystem::path& source,
                                   const BlenderInstall& blender, const AddonCache& cache,
                                   const std::vector<std::string>& shortNames, std::stop_token stop = {});

}
