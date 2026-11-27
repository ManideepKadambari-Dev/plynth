#include "plynth/core/ExportReady.h"

#include "BuiltPlan.h"

#include "plynth/core/ExportGroups.h"
#include "plynth/core/JobProgress.h"
#include "plynth/core/Library.h"
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
std::vector<ExportBlocker> exportBlockers(const fs::path& projectFolder, const MachineSettings& settings,
                                          const ExportNeeds& needs)
{
    std::vector<ExportBlocker> blockers;
    std::error_code ec;
    if (!fs::is_regular_file(buildFolder(projectFolder) / "building.blend", ec)) {
        blockers.push_back({"no_build", "Build the project first.", ErrorCode::NotFound});
    }
    if (jobRunning(projectFolder)) {
        blockers.push_back({"in_use", "A build or an export of this project is running.", ErrorCode::InUse});
    }
    if (needs.blender) {
        if (needs.blender->empty() || !fs::is_regular_file(*needs.blender, ec)) {
            blockers.push_back({"no_blender", "The Blender " + pathToUtf8(*needs.blender) + " does not exist.",
                                ErrorCode::BlenderNotFound});
        }
    } else {
        const SetupReport quick = quickSetupCheck(settings, true);
        const SetupItem* blender = quick.find(kCheckBlender);
        if (blender != nullptr && blender->blocks()) {
            blockers.push_back({"no_blender", blender->detail + (blender->action.empty() ? "" : " " + blender->action),
                                ErrorCode::BlenderNotFound});
        }
    }
    const ExportOptionsRead options = readExportOptions(projectFolder);
    const bool licenses = options.status.ok() && options.options.licenses.enabled;
    if (licenses || needs.kit) {
        const std::string what = "The export makes " + std::string(licenses ? "the license report" : "the modular kit") +
                                 ", which needs a library, ";
        const std::optional<fs::path> library = needs.library ? needs.library : settings.library;
        if (!library) {
            blockers.push_back({"no_library", what + "and no library folder is chosen.", ErrorCode::NoLibrary});
        } else if (!isLibraryFolder(*library)) {
            blockers.push_back({"no_library", what + "and " + pathToUtf8(*library) + " is not a Plynth library.",
                                ErrorCode::NoLibrary});
        }
    }
    return blockers;
}

}
