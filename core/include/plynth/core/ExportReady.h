#pragma once

#include "Build.h"
#include "Export.h"
#include "Project.h"
#include "Settings.h"
#include "Status.h"

#include <filesystem>
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

// What stops an export from starting, each with one plain sentence, in this order: no build/building.blend
// ("Build the project first."), then one for each required setup item that blocks (quickSetupCheck without the
// graphics probe: Blender, library and projects folders; "<name>: <action>", or the detail when there is no
// action, as the app's status line says it), then a live build/build.lock (jobRunning: "A build or an export of
// this project is running."). Empty when it may start. The export itself still makes its own checks.
struct ExportBlocker {
    std::string code;    // "no_build", "setup", "in_use"
    std::string message;
};

std::vector<ExportBlocker> exportBlockers(const std::filesystem::path& projectFolder, const MachineSettings& settings);

}
