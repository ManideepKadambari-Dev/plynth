#pragma once

#include "Blender.h"
#include "BuildJob.h"
#include "EditSession.h"
#include "Geometry.h"
#include "Library.h"
#include "Project.h"
#include "Status.h"
#include "catalog/AssetCatalog.h"

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
//   build/build.json       the report of the last build, schema/build-report.schema.json
//   build/job.json, build/result.json, build/blender.log   the last job, its result and Blender's output
//   build/build.lock       while a build runs
// Nothing in the project's plan or settings is changed.

inline constexpr std::string_view kBuildReportFileName = "build.json";
inline constexpr std::string_view kBuildLockFileName = "build.lock";
inline constexpr std::string_view kBuildReportFormat = "plynth-build-report";
inline constexpr int kBuildReportVersion = 1;

// How long a build may run before it is stopped, and after which a lock file left behind counts as stale.
inline constexpr std::chrono::seconds kBuildTimeout{1800};

// projectFolder / kBuildFolderName.
std::filesystem::path buildFolder(const std::filesystem::path& projectFolder);

// A short text that changes whenever the plan or the project settings change: 16 hex digits of a 64 bit
// FNV-1a hash of planToJson and settingsToJson. The report keeps it, so the app can say the plan changed
// since the last build.
std::string planFingerprint(const Project& project);

// A BuildAssetSource that asks the catalog. The catalog must outlive it.
BuildAssetSource buildAssetSourceFor(const AssetCatalog& catalog);

// Where on the plan a problem's object is, for the marker the app draws: a point's position, the middle of a
// wall's curve, an opening's centre, a room's label point, an item's centre. Plan coordinates. Empty when the
// problem has no object or the object is not in the plan.
std::optional<Vec2> problemPosition(const Plan& plan, const BuildProblem& problem);

struct BuildRequest {
    std::filesystem::path projectFolder;  // must exist; the plan is taken from project, not from the folder
    Project project;                      // the app passes what it has in memory, saved or not
    std::optional<LibraryPaths> library;  // empty: no library, items are left out (no_library)
    BlenderInstall blender;               // a usable install from chooseBlender
    BlenderRunOptions run{true, kBuildTimeout, {}};
    BuildSettings settings;
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
// the library's catalog, makeBuildJob, runBlenderJob in build/, readBuildResult, then writes build/build.json
// and removes the lock, also after a failure. A report is written for every build that got past the lock,
// failed or not. Blocks; the app runs it on a worker thread and passes stop for its Cancel button.
BuildReport runBuild(const BuildRequest& request, const std::function<void(const BuildProgress&)>& onProgress = {},
                     std::stop_token stop = {});

// The report in the form of schema/build-report.schema.json. Paths are absolute, UTF-8, forward slashes.
Json buildReportToJson(const BuildReport& report);

struct BuildReportRead {
    Status status; // NotFound when there is no build.json, BadFile when it is not a valid report
    BuildReport report;
};

// Reads projectFolder/build/build.json, for the app when it opens a project.
BuildReportRead readBuildReport(const std::filesystem::path& projectFolder);

}
