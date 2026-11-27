#pragma once

#include "Blender.h"
#include "BuildJob.h"
#include "EditSession.h"
#include "Geometry.h"
#include "Library.h"
#include "Project.h"
#include "Status.h"
#include "catalog/AssetCatalog.h"
#include "catalog/MaterialCatalog.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Building a project (DESIGN.md section 7.13): one call that the app's Build button and "plynth build" both
// use. It writes into the project's build/ folder (DESIGN.md section 5), which can always be deleted:
//   build/building.blend   the Blender file, a normal file the user can open and edit
//   build/building.fbx     what the 3D preview loads
//   build/materials.json   which image goes into which channel of each built material, for the 3D preview
//   build/textures/        constant images of the neutral looks
//   build/preview_textures/ copies of large images scaled to kPreviewTextureSize, used by building.fbx only
//   build/build.json       the report of the last build, schema/build-report.schema.json
//   build/built-plan.json  the plan and fingerprint of the last successful build, for the export checks
//   build/job.json, build/result.json, build/blender.log   the last job, its result and Blender's output
//   build/build.lock       while a build or an export runs (an export reads building.blend, so the two never
//                          run at the same time)
//   build/progress.json    while a build or an export runs, how far it has come (JobProgress.h)
//   build/export_work/     the job, result and Blender log of the last export (Export.h)
// Nothing in the project's plan or settings is changed.
//
// Generators and add-ons: a build reads the library's generators and the add-on cache, which counts only when
// it was made by the build's Blender, and always runs with factory startup. Add-ons are copied only from
// BuildRequest::addonSource, which prepareAddonFolder accepts only when it is the user's extensions folder of
// the build's Blender (Addons.h).
// A build would copy when addonSource is that folder and, by the saved cache or with no matching cache, an item
// to build needs add-ons. Only such a build asks Blender, and it always asks, however new the cache is:
//   - probeAddons runs once first, as it always runs (factory startup, private folders, preferences read as
//     data). The saved cache never decides what is copied: the build plan and the copy use the fresh result
//     alone, so an add-on switched off since the last check is not copied and the items that need it are left
//     out with generator_unavailable
//   - that probe may take the shorter of kAddonProbeTimeout and run.timeout, and it uses run.scriptsFolder.
//     Running out of time is a failed probe, only a stop cancels
//   - when the probe fails nothing is copied, runBuild gives one generator_unavailable warning and the build
//     goes on with no cache at all, the saved one included. When it is cancelled the build is Cancelled and
//     the build job is not started
//   - only the add-ons of the generators of items that are built are asked for, so one of an item that is left
//     out, or of a generator no item uses, is never read
//   - the job gets a new folder plynth-ext-<8 hex> in the temp folder (prepareAddonFolder). It is removed
//     however the run ends: success, failure, timeout, cancel or an exception, and only after Blender stopped
//   - a folder that a killed Plynth or a held file left behind is removed by a later build: each build first
//     removes folders of exactly that name form in the temp folder that are older than twice kBuildTimeout,
//     following no link
//   - a copy that fails or is refused gives one generator_unavailable warning, and the build runs on without it
// Any other build runs no probe and copies nothing: no item needs an add-on, the saved cache says the needed
// ones are off, or there is no addonSource. An addonSource that is not that folder is not probed for either
// and gives the warning of a refused copy. With no addonSource the worker reports generator_unavailable for
// the items that need an add-on.

inline constexpr std::string_view kBuildReportFileName = "build.json";
inline constexpr std::string_view kBuiltPlanFileName = "built-plan.json";
inline constexpr std::string_view kBuildLockFileName = "build.lock";
// Written by requestCancel; a running build or export stops when it appears (BuildLockFiles.h).
inline constexpr std::string_view kBuildCancelFileName = "cancel.request";
inline constexpr std::string_view kBuildReportFormat = "plynth-build-report";
inline constexpr int kBuildReportVersion = 1;

// How long a build may run before it is stopped, and after which a lock file left behind counts as stale.
inline constexpr std::chrono::seconds kBuildTimeout{1800};

// projectFolder / kBuildFolderName.
std::filesystem::path buildFolder(const std::filesystem::path& projectFolder);

// A short text that changes whenever the plan or the project settings change: 16 hex digits of a 64 bit
// FNV-1a hash of planToJson and settingsToJson. The report keeps it, so the app can say the plan changed
// since the last build. The export mode does not count: the settings are hashed with it set to single, so
// fingerprints of single projects stay what they were and switching the mode never makes a build out of date.
std::string planFingerprint(const Project& project);

// A BuildAssetSource that asks the catalog. The catalog must outlive it.
BuildAssetSource buildAssetSourceFor(const AssetCatalog& catalog);

// A BuildMaterialSource that asks the catalog and reads each material's info.json once. The catalog must
// outlive it.
BuildMaterialSource buildMaterialSourceFor(const MaterialCatalog& catalog);

// Where on the plan a problem's object is, for the marker the app draws: a point's position, the middle of a
// wall's curve, an opening's centre, a room's label point, an item's centre, a stair's bottom step (x, y), a
// roof section entry's anchor. Plan coordinates. Empty when the problem has no object or the object is not
// in the plan.
std::optional<Vec2> problemPosition(const Plan& plan, const BuildProblem& problem);

struct BuildRequest {
    std::filesystem::path projectFolder;  // must exist; the plan is taken from project, not from the folder
    Project project;                      // the app passes what it has in memory, saved or not
    std::optional<LibraryPaths> library;  // empty: no library, items are left out (no_library)
    BlenderInstall blender;               // a usable install from chooseBlender
    BlenderRunOptions run{true, kBuildTimeout, {}};
    BuildSettings settings;
    // Where add-ons may be copied from for this build. The app's Build and "plynth build" set it to
    // blenderUserFolders(blender).extensions after choosing Blender, and nothing else sets it. Empty, the
    // default: none are copied. runBuild never fills it in. run.extensionsFolder is not used.
    std::filesystem::path addonSource;
};

enum class BuildStage {
    Preparing, // "preparing": checks, the build plan and the job
    Blender,   // "blender": the worker runs; step, index and count come from it
    Finishing, // "finishing": reading the result and writing build.json
    Done,      // "done"
};

std::string_view buildStageName(BuildStage stage);

struct BuildProgress {
    BuildStage stage = BuildStage::Preparing;
    BlenderProgress blender; // filled during the Blender stage
};

// The report of one build. Also the CLI's result and build/build.json (buildReportToJson).
struct BuildReport {
    // Ok when the worker finished, even with problems. InvalidValue from checkBuildable, NotFound when the
    // project folder does not exist, InUse when another build holds the lock, BlenderNotFound, BlenderFailed
    // (the worker's message, with the log file named), Cancelled, CannotWrite.
    Status status;
    std::string errorCode;         // the worker's error code when it gave one, for example "save_failed"
    std::string jobId;             // "build-3f9a07c1"
    SessionTime startedAt{};
    long long elapsedMs = 0;
    std::string blenderVersion;
    std::string planFingerprint;
    std::filesystem::path blendFile; // absolute; empty when the build failed
    std::filesystem::path fbxFile;   // absolute; empty when the build failed or none was asked for
    std::filesystem::path materialsFile; // build/materials.json, absolute; empty when fbxFile is
    std::filesystem::path logFile;   // build/blender.log, once Blender ran
    long long objects = 0;
    long long triangles = 0;
    BuildCounts counts;
    std::vector<BuildStepTime> steps; // "check" from core first, then the worker's steps in order
    // Core's problems (makeBuildPlan), then the worker's, each list in its own order.
    std::vector<BuildProblem> problems;

    int errorCount() const;
    int warningCount() const;
};

// Builds a project: checkBuildable, takes build/build.lock (InUse while another build holds a lock that is
// younger than kBuildTimeout and whose process still runs; a stale lock is taken over), makeBuildPlan with
// the library's catalogs and generators, makeBuildJob, runBlenderJob in build/, readBuildResult, then writes build/build.json
// and removes the lock, also after a failure. A report is written for every build that got past the lock,
// failed or not. Blocks; the app runs it on a worker thread and passes stop for its Cancel button.
BuildReport runBuild(const BuildRequest& request, const std::function<void(const BuildProgress&)>& onProgress = {},
                     std::stop_token stop = {});

// Asks the build or the export running on a project, in this process or another, to stop: writes
// build/cancel.request, which the running job watches. False, writing nothing, when no live lock says one runs.
// The app's Cancel button, "plynth build cancel" and "plynth export cancel" all end up here or at the job's
// own stop token.
bool requestCancel(const std::filesystem::path& projectFolder);

// The report in the form of schema/build-report.schema.json. Paths are absolute, UTF-8, forward slashes.
Json buildReportToJson(const BuildReport& report);

struct BuildReportRead {
    Status status; // NotFound when there is no build.json, BadFile when it is not a valid report
    BuildReport report;
};

// Reads projectFolder/build/build.json, for the app when it opens a project. A report written before milestone
// 9 has no materials count and no materials_file; they read as 0 and empty. One written before milestone 10
// has no collision count; it reads as 0. So does a missing generators count.
BuildReportRead readBuildReport(const std::filesystem::path& projectFolder);

// build/materials.json, schema/build-materials.schema.json: for each Blender material of the built surfaces
// (one per slot, MaterialSlots.h), the images of its channels, and for each built object the material of each
// of its slots in order. The 3D preview replaces the materials the FBX loader made with these, matched by the
// base color image path. Several slots may share one catalog material and so one base color path; their
// entries then have the same maps, and the first one found is used. The export (Export.h) reads this file to
// name the textures it copies and for the tile size each material was built with, so the modular kit's UVs
// match the building's after the library changes.
inline constexpr std::string_view kBuildMaterialsFormat = "plynth-build-materials";
inline constexpr int kBuildMaterialsVersion = 1;

enum class AlphaMode {
    Opaque, // "opaque"
    Blend,  // "blend": the opacity map is used
};

struct BuiltMaterial {
    std::string name;                 // the Blender and FBX material name: the slot name, for example "Living_Floor"
    std::string material;             // the catalog id; empty for a neutral look
    std::string surface;              // the neutral surface name (for example "Glass"); empty for a catalog one
    std::filesystem::path baseColor;  // absolute
    std::filesystem::path normal;
    std::filesystem::path roughness;
    std::filesystem::path metallic;
    std::filesystem::path opacity;    // empty when opaque
    std::filesystem::path occlusion;  // empty when there is none
    AlphaMode alphaMode = AlphaMode::Opaque;
    std::string asset;                // the asset id for a material of furniture; empty for a built surface
    std::optional<TileSize> tileSize; // what the build divided a catalog material's UVs by (tile_size in the file);
                                      // empty for a neutral look, for furniture and when the file has none

    bool operator==(const BuiltMaterial&) const = default;
};

struct BuiltObjectSlots {
    std::string object;              // the Blender and FBX object name, for example "f1_Walls"
    std::vector<std::string> slotNames; // BuiltMaterial names in slot order

    bool operator==(const BuiltObjectSlots&) const = default;
};

struct BuiltMaterialsRead {
    Status status; // NotFound when there is no materials.json, BadFile when it is not valid
    std::vector<BuiltMaterial> materials;
    std::vector<BuiltObjectSlots> objects;
};

// Reads projectFolder/build/materials.json.
BuiltMaterialsRead readBuiltMaterials(const std::filesystem::path& projectFolder);

}
