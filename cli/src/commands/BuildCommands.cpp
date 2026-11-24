#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/Build.h>
#include <plynth/core/BuildJob.h>
#include <plynth/core/Blender.h>
#include <plynth/core/Generators.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct BuildOptions {
    std::string project;
    std::string library;
    std::string blender;
    bool quiet = false;
    bool check = false;
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
std::string stageMessage(BuildStage stage) {
    switch (stage) {
    case BuildStage::Preparing:
        return "Checking the plan";
    case BuildStage::Finishing:
        return "Writing the report";
    case BuildStage::Blender:
    case BuildStage::Done:
        break;
    }
    return "Done";
}

// The problems of a build plan as a JSON list.
Json problemsJson(const std::vector<BuildProblem>& problems) {
    Json list = Json::array();
    for (const BuildProblem& problem : problems) {
        list.push_back(buildProblemToJson(problem));
    }
    return list;
}

// Opens the library when it can be opened. Any failure means no library: the items are then reported
// as no_library instead of the command being refused.
bool tryOpenCatalog(const std::string& command, const RunContext& context, const std::string& libraryOption,
                    OpenedCatalog& opened) {
    Answer ignored;
    return openCatalog(command, context, libraryOption, opened, ignored);
}

// Reports the problems of a build without running Blender or writing anything. The library's generators are
// checked, but no add-on cache is read, so the check says nothing about which add-ons are on.
Answer buildCheck(const std::string& command, const Project& project, const OpenedCatalog* opened) {
    BuildAssetSource source;
    GeneratorLibrary generators;
    if (opened != nullptr) {
        source = buildAssetSourceFor(*opened->catalog);
        generators = loadGeneratorFolder(opened->paths.generators);
    }
    const BuildPlan plan =
        makeBuildPlan(project, source, {}, opened != nullptr ? &generators : nullptr, std::nullopt);
    int errors = 0;
    int warnings = 0;
    for (const BuildProblem& problem : plan.problems) {
        (problem.severity == BuildSeverity::Error ? errors : warnings) += 1;
    }
    Json result = Json::object();
    result["problems"] = problemsJson(plan.problems);
    result["error_count"] = errors;
    result["warning_count"] = warnings;
    result["plan_fingerprint"] = planFingerprint(project);
    return success(command, std::move(result));
}

// Builds the project in Blender, or only checks it with --check.
Answer buildProject(const RunContext& context, const BuildOptions& options) {
    const std::string command = "build";
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const Status buildable = checkBuildable(document.plan());
    if (!buildable.ok()) {
        return failure(command, buildable);
    }

    OpenedCatalog opened;
    const bool haveLibrary = tryOpenCatalog(command, context, options.library, opened);
    if (options.check) {
        return buildCheck(command, document.project(), haveLibrary ? &opened : nullptr);
    }

    if (!haveLibrary) {
        const SettingsLoad loaded = loadSettings();
        if (!loaded.status.ok()) {
            return failure(command, loaded.status);
        }
        opened.settings = loaded.settings;
    }
    std::optional<std::filesystem::path> blenderOption;
    if (!options.blender.empty()) {
        blenderOption = userPath(context, options.blender);
    }
    const BlenderChoice choice = chooseBlender(blenderOption, opened.settings);
    if (!choice.status.ok()) {
        return failure(command, choice.status);
    }

    BuildRequest request;
    request.projectFolder = folder;
    request.project = document.project();
    if (haveLibrary) {
        request.library = opened.paths;
    }
    request.blender = choice.install;
    request.addonSource = blenderUserFolders(choice.install).extensions;

    std::ostream* progress = options.quiet ? nullptr : context.progress;
    std::optional<BuildStage> lastStage;
    const auto onProgress = [&](const BuildProgress& step) {
        if (progress == nullptr) {
            return;
        }
        if (step.stage == BuildStage::Blender) {
            if (step.blender.index < 1) {
                return;
            }
            writeProgress(*progress, buildStageName(step.stage), step.blender, step.blender.message);
        } else if (!lastStage || *lastStage != step.stage) {
            writeProgress(*progress, buildStageName(step.stage), BlenderProgress{}, stageMessage(step.stage));
        }
        lastStage = step.stage;
    };
    const BuildReport report = runBuild(request, onProgress);
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Answer answer = success(command, buildReportToJson(report));
    for (const BuildProblem& problem : report.problems) {
        answer.warnings.push_back({problem.code, problem.message});
    }
    return answer;
}

}

std::vector<CommandSpec> buildCommands() {
    CommandSpec build;
    build.words = {"build"};
    build.description = "Build the project in Blender: walls, openings, floors, ceilings, furniture and "
                        "generators. With --check, only list the problems.";
    build.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<BuildOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_flag("--quiet", options->quiet, "Write no progress lines to standard error.");
        sub.add_flag("--check", options->check, "List the problems without running Blender or writing anything.");
        return [options](const RunContext& context) { return buildProject(context, *options); };
    };
    return {build};
}

}
