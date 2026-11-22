#pragma once

#include "Blender.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The files passed between C++ and the Blender worker (blender/plynth_worker.py). C++ writes a job file,
// starts Blender with the worker script, and reads the result file the worker writes when it ends.
//
// Job file, schema/blender-job.schema.json:
//   {"format": "plynth-blender-job", "version": 1, "task": "cleanup_asset", "job_id": "chair-3f9a",
//    "result_file": "D:/Library/assets/.staging/chair-3f9a/result.json", "params": {...}}
// Result file, schema/blender-result.schema.json:
//   {"format": "plynth-blender-result", "version": 1, "task": "cleanup_asset", "job_id": "chair-3f9a",
//    "ok": true, "error": null, "blender_version": "5.2.0", "warnings": [], "elapsed_ms": 4210, "output": {...}}
//   With ok false, error is {"code": "import_failed", "message": "One plain sentence."} and output is null.
// The worker writes the result through a temporary file and a rename, so a half written result is never read.
// All paths are absolute, UTF-8, forward slashes. All lengths are metres.
//
// Progress: while it runs the worker prints lines to standard output of the form
//   PLYNTH:{"progress": {"step": "apply_transforms", "index": 3, "count": 11, "message": "Applying transforms"}}
// Every other line is log text.
//
// Worker error codes: bad_job, unknown_task, unsupported_format, import_failed, no_geometry, several_models,
// several_materials, no_base_color, bake_failed, unpack_failed, save_failed, render_failed, export_failed,
// preferences_unreadable, internal.
// Of store fetches (catalog/BlenderKit.h says when): addon_missing, not_signed_in, download_failed,
// download_timeout, wrong_asset_kind.

inline constexpr std::string_view kBlenderJobFormat = "plynth-blender-job";
inline constexpr int kBlenderJobVersion = 1;
inline constexpr std::string_view kBlenderResultFormat = "plynth-blender-result";
inline constexpr int kBlenderResultVersion = 1;

inline constexpr std::string_view kJobFileName = "job.json";
inline constexpr std::string_view kResultFileName = "result.json";
inline constexpr std::string_view kBlenderLogFileName = "blender.log";
inline constexpr std::string_view kProgressPrefix = "PLYNTH:";

// Task ids. Each is one file in blender/plynth/tasks/. A store's fetch task has its id in the store's header:
// "blenderkit_fetch" is kBlenderKitFetchTask in catalog/BlenderKit.h, and its output is the shape every fetch
// task reports, see catalog/StoreFetch.h.
inline constexpr std::string_view kProbeTask = "probe";                     // version and add-ons, see SetupCheck.h
inline constexpr std::string_view kCleanupAssetTask = "cleanup_asset";     // see catalog/AssetJobs.h
inline constexpr std::string_view kRenderThumbnailTask = "render_thumbnail"; // see catalog/AssetJobs.h
inline constexpr std::string_view kBuildTask = "build";                    // see BuildJob.h
inline constexpr std::string_view kCleanupMaterialTask = "cleanup_material"; // see catalog/MaterialJobs.h
inline constexpr std::string_view kExportTask = "export";                  // see ExportJob.h

// Makes a job: format, version, task, job_id, result_file and params, in that order.
Json makeBlenderJob(std::string_view task, std::string_view jobId, const std::filesystem::path& resultFile,
                    Json params);

// A new job id: the given stem (an asset id, or the task) and 8 random hex digits, for example "chair-3f9a07c1".
std::string newJobId(std::string_view stem);

// A note from the worker that did not stop the job, for example {"code": "high_polycount", ...}.
// Codes: high_polycount, scale_suspicious, missing_texture, removed_objects, unsupported_object,
// fbx_failed (asset.fbx for the live preview could not be written; the asset itself is fine),
// material_approximated (an asset material's maps could not show it exactly, see catalog/AssetJobs.h).
// Material codes (catalog/MaterialJobs.h): ambiguous_map, unused_image, map_size_mismatch, visible_seams,
// low_resolution. The cleanup of a Fab model gives ambiguous_map and unused_image too.
struct BlenderWarning {
    std::string code;
    std::string message;

    bool operator==(const BlenderWarning&) const = default;
};

// The fields every result has, read and checked. BadFile when the format, version, task or job id does not
// match what was asked, or a field is missing. When ok is false, status is BlenderFailed with the worker's
// message and errorCode holds its code.
struct BlenderResultHeader {
    Status status;
    std::string errorCode;
    std::string blenderVersion;
    std::vector<BlenderWarning> warnings;
    long long elapsedMs = 0;
};

BlenderResultHeader readBlenderResultHeader(const Json& result, std::string_view task, std::string_view jobId);

// The probe task. It runs with factory startup in Plynth's private folder and reads the user's saved Blender
// settings as data: it loads no settings, imports no add-on and writes nothing in the user's folders.
// Params, all required, absolute paths:
//   {"user_folders": {"config": "...", "scripts": "...", "extensions": "...", "blenderkit_data": "..."}}
// The worker has no default for them and answers bad_job without them. Output:
//   {"blender": {"version": "5.2.0", "version_text": "5.2.0 LTS", "python": "3.11.9", "build_date": "2026-07-14"},
//    "addons": [{"module": "bl_ext.user_default.blenderkit", "name": "BlenderKit Online Asset Library",
//                "version": "3.14.0"}],
//    "blenderkit": {"installed": true, "enabled": true, "signed_in": false}}
// addons lists only add-ons and extensions that are both saved as enabled and found on disk. Enabled does not
// prove that one loads. installed means found on disk. signed_in only says whether a BlenderKit sign-in is
// saved, not that it is still valid; the key itself is never output or written anywhere.
// No saved settings means Blender's defaults and not signed in. Settings that cannot be read give the error
// preferences_unreadable, with a fixed sentence that holds nothing from the file.
struct ProbeAddon {
    std::string module;
    std::string name;
    std::string version;
};

struct ProbeResult {
    BlenderResultHeader header;
    std::string version;
    std::string python;
    std::vector<ProbeAddon> addons;
    bool blenderKitInstalled = false;
    bool blenderKitEnabled = false;
    bool blenderKitSignedIn = false;
};

ProbeResult readProbeResult(const Json& result, std::string_view jobId);

// Makes a probe job with the given user folders.
Json makeProbeJob(const BlenderUserFolders& folders, std::string_view jobId, const std::filesystem::path& resultFile);

struct BlenderProbeRun {
    // Ok when result can be used. Otherwise why not; result.header.errorCode holds the worker's code when it
    // gave one.
    Status status;
    ProbeResult result;
};

// Asks one Blender about the user's add-ons. Works out the user's folders before the child's environment is
// replaced, runs the probe job in a temporary work folder and removes that folder however the run ends.
// options.factoryStartup is ignored. Fails without starting Blender when the install is not usable or a
// folder cannot be worked out. A failed probe is never run again with normal startup. Blocks.
BlenderProbeRun runBlenderProbe(const BlenderInstall& install, const BlenderRunOptions& options = {},
                                std::stop_token stop = {});

}
