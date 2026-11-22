#pragma once

#include "Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
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
// unpack_failed, save_failed, render_failed, export_failed, internal.

inline constexpr std::string_view kBlenderJobFormat = "plynth-blender-job";
inline constexpr int kBlenderJobVersion = 1;
inline constexpr std::string_view kBlenderResultFormat = "plynth-blender-result";
inline constexpr int kBlenderResultVersion = 1;

inline constexpr std::string_view kJobFileName = "job.json";
inline constexpr std::string_view kResultFileName = "result.json";
inline constexpr std::string_view kBlenderLogFileName = "blender.log";
inline constexpr std::string_view kProgressPrefix = "PLYNTH:";

// Task ids. Each is one file in blender/plynth/tasks/.
inline constexpr std::string_view kProbeTask = "probe";                     // version and add-ons, see SetupCheck.h
inline constexpr std::string_view kCleanupAssetTask = "cleanup_asset";     // see catalog/AssetJobs.h
inline constexpr std::string_view kRenderThumbnailTask = "render_thumbnail"; // see catalog/AssetJobs.h
inline constexpr std::string_view kBuildTask = "build";                    // see BuildJob.h

// Makes a job: format, version, task, job_id, result_file and params, in that order.
Json makeBlenderJob(std::string_view task, std::string_view jobId, const std::filesystem::path& resultFile,
                    Json params);

// A new job id: the given stem (an asset id, or the task) and 8 random hex digits, for example "chair-3f9a07c1".
std::string newJobId(std::string_view stem);

// A note from the worker that did not stop the job, for example {"code": "high_polycount", ...}.
// Codes: high_polycount, scale_suspicious, missing_texture, removed_objects, unsupported_object,
// fbx_failed (asset.fbx for the live preview could not be written; the asset itself is fine).
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

// The probe task. Params: {} . Output:
//   {"blender": {"version": "5.2.0", "version_text": "5.2.0 LTS", "python": "3.11.9", "build_date": "2026-07-14"},
//    "addons": [{"module": "bl_ext.user_default.blenderkit", "name": "BlenderKit Online Asset Library",
//                "version": "3.14.0"}],
//    "blenderkit": {"installed": true, "enabled": true, "signed_in": false}}
// addons lists only enabled add-ons and extensions. signed_in only says whether an API key is stored in the
// add-on's preferences; the key itself is never read out or written anywhere.
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

}
