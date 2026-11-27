#pragma once

#include "Build.h"
#include "Export.h"
#include "Project.h"
#include "Settings.h"
#include "Status.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace plynth {

// What the app's Export window and "plynth export" check before an export, and how both describe the last export
// report (issue 51).

// Says whether an export report describes an older build than the last good one: its buildFingerprint differs
// from the fingerprint of the last successful build (build/built-plan.json, else build/build.json when that build
// succeeded, the rule runExport uses), or export/export_report.json is older than build/building.blend. A failed
// build rewrites build.json but leaves the old building.blend, so it never makes a report out of date. False when
// export_report.json or build.json is missing or build.json cannot be read.
bool exportReportOutOfDate(const ExportReport& report, const std::filesystem::path& projectFolder);

// What an export is asked to use, for exportBlockers. The app leaves both paths empty.
struct ExportNeeds {
    std::optional<std::filesystem::path> blender; // the --blender option; empty: PLYNTH_BLENDER, the settings, a search
    std::optional<std::filesystem::path> library; // the --library option; empty: the library of the settings
    bool kit = false; // the Unreal link's kit switch; the plain export never makes the kit
};

// One reason an export cannot start, with the error a front end refuses with.
struct ExportBlocker {
    std::string code;    // "no_build", "in_use", "no_blender", "no_library"
    std::string message;
    ErrorCode error = ErrorCode::InvalidValue; // NotFound, InUse, BlenderNotFound, NoLibrary in that order
};

// What stops an export from starting, only what an export really needs, in this order: no build/building.blend
// ("Build the project first."); a live build/build.lock (jobRunning: "A build or an export of this project is
// running."); no Blender to run it (the option when given must be a file, otherwise the Blender item of
// quickSetupCheck, which starts no program: its detail and action); and, only when export-options.json switches
// the license report on or needs.kit is set, a library (the option, else the settings) that is a Plynth library.
// A missing library or projects folder blocks nothing else. Empty when it may start. The export itself still
// makes its own checks, such as the Blender version.
std::vector<ExportBlocker> exportBlockers(const std::filesystem::path& projectFolder, const MachineSettings& settings,
                                          const ExportNeeds& needs = {});

}
