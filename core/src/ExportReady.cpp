#include "plynth/core/ExportReady.h"

#include "BuiltPlan.h"

#include "plynth/core/JobProgress.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/SetupCheck.h"

#include <optional>
#include <string>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

// Says whether the export report came from an older build than the last good one.
bool exportReportOutOfDate(const ExportReport& report, const fs::path& projectFolder)
{
    const fs::path reportFile = exportFolder(projectFolder) / pathFromUtf8(kExportReportFileName);
    const fs::path buildFile = buildFolder(projectFolder) / pathFromUtf8(kBuildReportFileName);
    const fs::path blendFile = buildFolder(projectFolder) / "building.blend";
    std::error_code ec;
    if (!fs::is_regular_file(reportFile, ec) || !fs::is_regular_file(buildFile, ec)) return false;
    const BuildReportRead build = readBuildReport(projectFolder);
    if (!build.status.ok()) return false;
    // As in the export: a failed build leaves the old building.blend, so only the last successful build counts.
    std::string lastGood;
    if (const std::optional<BuiltPlan> built = readBuiltPlan(projectFolder)) {
        lastGood = built->fingerprint;
    } else if (build.report.status.ok()) {
        lastGood = build.report.planFingerprint;
    }
    if (!lastGood.empty() && report.buildFingerprint != lastGood) return true;
    if (!fs::is_regular_file(blendFile, ec)) return false;
    const auto reportTime = fs::last_write_time(reportFile, ec);
    if (ec) return false;
    const auto blendTime = fs::last_write_time(blendFile, ec);
    if (ec) return false;
    return reportTime < blendTime;
}

// Lists what stops an export from starting now.
std::vector<ExportBlocker> exportBlockers(const fs::path& projectFolder, const MachineSettings& settings)
{
    std::vector<ExportBlocker> blockers;
    std::error_code ec;
    if (!fs::is_regular_file(buildFolder(projectFolder) / "building.blend", ec)) {
        blockers.push_back({"no_build", "Build the project first."});
    }
    const SetupReport quick = quickSetupCheck(settings, true);
    for (const SetupItem& item : quick.items) {
        if (!item.blocks()) continue;
        blockers.push_back({"setup", item.name + ": " + (item.action.empty() ? item.detail : item.action)});
    }
    if (jobRunning(projectFolder)) {
        blockers.push_back({"in_use", "A build or an export of this project is running."});
    }
    return blockers;
}

}
