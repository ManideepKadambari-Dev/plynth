#pragma once

#include "Blender.h"
#include "EditSession.h"
#include "ExportJob.h"
#include "Project.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Exporting a project for Unreal (DESIGN.md sections 7.16 and 7.17): one call that the app's Export button and
// "plynth export" both use. It exports the last build (build/building.blend, with any hand edits); it never
// builds. It writes into the project's export/ folder (DESIGN.md section 5), which can always be deleted:
//   export/<Name>.fbx           one file per group of the project's exporter (Exporter.h)
//   export/Textures/            the images of every exported material (ExportJob.h names them)
//   export/export_report.json   the report of the last export, written for every export that got past the
//                               lock, blocked or not (schema/export-report.schema.json)
// The worker writes into export/.staging/; only when the export is not blocked does core remove the files the
// previous report lists and the old Textures/ folder, and move the new ones in. A blocked or failed export
// leaves the last good export in place. The job, result and Blender log go to build/export_work/.
// While it runs it holds build/build.lock, the build's lock, so a build cannot replace building.blend under it.
// Nothing in the project's plan or settings is changed.

inline constexpr std::string_view kExportFolderName = "export";
inline constexpr std::string_view kExportStagingFolder = ".staging";
inline constexpr std::string_view kExportReportFileName = "export_report.json";
inline constexpr std::string_view kExportWorkFolder = "export_work"; // inside build/
inline constexpr std::string_view kExportReportFormat = "plynth-export-report";
inline constexpr int kExportReportVersion = 1;

// projectFolder / kExportFolderName.
std::filesystem::path exportFolder(const std::filesystem::path& projectFolder);

struct ExportRequest {
    std::filesystem::path projectFolder; // must exist and hold build/building.blend
    Project project;                     // the app passes what it has in memory, saved or not
    BlenderInstall blender;              // a usable install from chooseBlender
    BlenderRunOptions run{true, kBuildTimeout, {}};
    ExportSettings settings;
};

enum class ExportStage {
    Preparing, // "preparing": the exporter's groups, the last build's report and materials, the job
    Blender,   // "blender": the worker runs; step, index and count come from it
    Finishing, // "finishing": moving the files into export/ and writing the report
    Done,      // "done"
};

std::string_view exportStageName(ExportStage stage);

struct ExportProgress {
    ExportStage stage = ExportStage::Preparing;
    BlenderProgress blender; // filled during the Blender stage
};

// The report of one export. Also the CLI's result and export/export_report.json (exportReportToJson).
struct ExportReport {
    // Ok when the files were exported, or when settings.write was false and the checks ran. QualityFailed when a
    // check failed and the export was not forced (nothing in export/ changed, the report is written).
    // InvalidValue when the project's export mode has no exporter yet ("Grouped export is not available
    // yet ...") or the exporter refuses. NotFound when the project folder or build/building.blend does not exist
    // ("Build the project first."). InUse when a build or another export holds the lock. BlenderNotFound,
    // BlenderFailed (the worker's message, with the log file named), Cancelled, CannotWrite.
    Status status;
    std::string errorCode;      // the worker's error code when it gave one, for example "no_geometry"
    std::string jobId;          // "export-3f9a07c1"
    SessionTime startedAt{};
    long long elapsedMs = 0;
    std::string blenderVersion;
    std::string exporter;       // the exporter id, for example "single"
    std::string planFingerprint;  // of the project as exported
    std::string buildFingerprint; // the plan_fingerprint of build/build.json; empty when there is none
    bool exported = false;      // the files are now in export/
    bool checkedOnly = false;   // settings.write was false
    bool forced = false;        // settings.force was true
    std::vector<std::string> overridden; // ids of the failed checks a forced export went past
    std::vector<std::filesystem::path> files; // absolute, in export/; empty unless exported
    std::vector<ExportFileOutput> fileOutputs; // group, name and counts of each entry of files, same order
    std::filesystem::path texturesFolder;     // export/Textures, absolute; empty unless exported
    int textureCount = 0;
    std::filesystem::path reportFile;         // export/export_report.json, absolute
    std::filesystem::path logFile;            // build/export_work/blender.log, once Blender ran
    long long objects = 0;
    long long triangles = 0;
    long long collisionObjects = 0;
    std::vector<QualityCheckResult> checks;   // in the order the worker ran them
    std::vector<BuildStepTime> steps;         // "prepare" from core first, then the worker's steps

    bool passed() const;      // no check failed
    int failCount() const;
    int warningCount() const;
};

// Exports a project: the exporter for its export mode (findExporter(exportModeName(mode))), the project folder
// and build/building.blend, takes build/build.lock (as runBuild does, a stale lock is taken over), reads
// build/build.json (readBuildReport: the plan changed when its fingerprint differs from planFingerprint, or
// there is none) and build/materials.json (readBuiltMaterials; missing is an empty list), empties
// export/.staging, makeExportJob, runBlenderJob in build/export_work, readExportResult; then moves the files
// into export/ when written and (passed or forced), removes export/.staging, writes
// export/export_report.json and removes the lock, also after a failure. Blocks; the app runs it on a worker
// thread and passes stop for its Cancel button.
ExportReport runExport(const ExportRequest& request,
                       const std::function<void(const ExportProgress&)>& onProgress = {},
                       std::stop_token stop = {});

// The report in the form of schema/export-report.schema.json. Paths are absolute, UTF-8, forward slashes.
Json exportReportToJson(const ExportReport& report);

struct ExportReportRead {
    Status status; // NotFound when there is no export_report.json, BadFile when it is not a valid report
    ExportReport report;
};

// Reads projectFolder/export/export_report.json, for the app when it opens a project and for
// "plynth export report".
ExportReportRead readExportReport(const std::filesystem::path& projectFolder);

}
