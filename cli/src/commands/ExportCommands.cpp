#include "../CatalogCommon.h"
#include "../Common.h"
#include "../SessionKeeper.h"

#include <plynth/core/Blender.h>
#include <plynth/core/Export.h>
#include <plynth/core/Settings.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct ExportOptions {
    std::string what;
    std::string project;
    std::string blender;
    bool quiet = false;
    bool check = false;
    bool force = false;
};

// Writes one progress line: {"progress": {"stage", "step", "index", "count", "message"}}.
void writeProgress(std::ostream& out, std::string_view stage, const BlenderProgress& step,
                   const std::string& message) {
    Json inner = Json::object();
    inner["stage"] = std::string(stage);
    inner["step"] = step.step;
    inner["index"] = step.index;
    inner["count"] = step.count;
    inner["message"] = message;
    Json line = Json::object();
    line["progress"] = std::move(inner);
    out << line.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    out.flush();
}

// The sentence shown for a stage that has no worker step.
std::string stageMessage(ExportStage stage) {
    switch (stage) {
    case ExportStage::Preparing:
        return "Preparing the export";
    case ExportStage::Finishing:
        return "Writing the files and the report";
    case ExportStage::Blender:
    case ExportStage::Done:
        break;
    }
    return "Done";
}

// Answers with the last export report of the project.
Answer exportReport(const std::string& command, const ExportOptions& options, const RunContext& context) {
    if (options.check || options.force) {
        return failure(command, fail(ErrorCode::InvalidValue, "export report takes no --check or --force."));
    }
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const ExportReportRead read = readExportReport(folder);
    if (!read.status.ok()) {
        return failure(command, read.status);
    }
    return success(command, exportReportToJson(read.report));
}

// Exports the last build, or only runs the checks with --check.
Answer exportProject(const RunContext& context, const ExportOptions& options) {
    const std::string command = "export";
    if (options.what == "report") {
        return exportReport(command, options, context);
    }
    if (options.what == "cancel") {
        return cancelRunning("export", context, options.project);
    }
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }

    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    std::optional<std::filesystem::path> blenderOption;
    if (!options.blender.empty()) {
        blenderOption = userPath(context, options.blender);
    }
    const BlenderChoice choice = chooseBlender(blenderOption, loaded.settings);
    if (!choice.status.ok()) {
        return failure(command, choice.status);
    }

    ExportRequest request;
    request.projectFolder = folder;
    request.project = document.project();
    request.blender = choice.install;
    request.settings.force = options.force;
    request.settings.write = !options.check;

    std::ostream* progress = options.quiet ? nullptr : context.progress;
    std::optional<ExportStage> lastStage;
    const auto onProgress = [&](const ExportProgress& step) {
        if (progress == nullptr) {
            return;
        }
        if (step.stage == ExportStage::Blender) {
            if (step.blender.index < 1) {
                return;
            }
            writeProgress(*progress, exportStageName(step.stage), step.blender, step.blender.message);
        } else if (!lastStage || *lastStage != step.stage) {
            writeProgress(*progress, exportStageName(step.stage), BlenderProgress{}, stageMessage(step.stage));
        }
        lastStage = step.stage;
    };
    const SessionKeeper keeper(folder);
    const ExportReport report = runExport(request, onProgress, stopToken(context));
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Answer answer = success(command, exportReportToJson(report));
    if (options.check) {
        for (const QualityCheckResult& check : report.checks) {
            if (check.status == QualityStatus::Fail) {
                answer.warnings.push_back({"quality_failed", "The check '" + check.id + "' failed."});
            }
        }
    }
    return answer;
}

}

std::vector<CommandSpec> exportCommands() {
    CommandSpec spec;
    spec.words = {"export"};
    spec.description = "Export the last build as FBX files for Unreal behind the quality checks. "
                       "With --check, only run the checks. With --force, export past failed checks. "
                       "'export report' shows the last export report and 'export cancel' stops a running export.";
    spec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ExportOptions>();
        sub.add_option("what", options->what,
                       "Use 'report' to show the last export report, or 'cancel' to stop the export running on the project.")
            ->check(CLI::IsMember({"report", "cancel"}));
        addProjectOption(sub, options->project);
        addBlenderOption(sub, options->blender);
        sub.add_flag("--quiet", options->quiet, "Write no progress lines to standard error.");
        sub.add_flag("--check", options->check, "Run the checks without writing any export files.");
        sub.add_flag("--force", options->force, "Export even when a check failed.");
        return [options](const RunContext& context) { return exportProject(context, *options); };
    };
    return {spec};
}

}
