#include "../CatalogCommon.h"
#include "../Common.h"
#include "../SessionKeeper.h"

#include <plynth/core/Pack.h>

#include <memory>
#include <string>

namespace plynth::cli {

namespace {

struct PackOptions {
    std::string project;
    std::string library;
    bool check = false;
};

struct UnpackCliOptions {
    std::string project;
    std::string library;
    bool check = false;
    bool keepLibrary = false;
};

// Copies the library items the project uses into its pack/ folder, so it can move to another machine.
Answer packCommand(const RunContext& context, const PackOptions& options) {
    const std::string command = "pack";
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    OpenedCatalog opened;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    opened.catalog.reset();

    PackRequest request;
    request.projectFolder = folder;
    request.contents = packContents(document.project(), opened.paths);
    request.library = opened.paths;
    request.checkOnly = options.check;
    const SessionKeeper keeper(folder);
    const PackReport report = packProject(request, stopToken(context));
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Answer answer = success(command, packReportToJson(report));
    for (const BuildProblem& problem : report.problems) {
        answer.warnings.push_back({problem.code, problem.message});
    }
    return answer;
}

// Copies a pack's items into this machine's library, or with --check only says what it would do.
Answer unpackCommand(const RunContext& context, const UnpackCliOptions& options) {
    const std::string command = "unpack";
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Answer failed;
    if (!checkLock(command, folder, true, failed)) {
        return failed;
    }
    if (!isProjectFolder(folder)) {
        return failure(command, fail(ErrorCode::NotAProject, "The folder " + pathToUtf8(folder) + " holds no project."));
    }
    OpenedCatalog opened;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    opened.catalog.reset();

    UnpackReport report;
    if (options.check) {
        const UnpackPlan plan = planUnpack(folder, opened.paths);
        report.status = plan.status;
        report.entries = plan.entries;
        report.complete = true;
        for (const UnpackEntry& entry : plan.entries) {
            if (entry.action != UnpackAction::Add && entry.action != UnpackAction::Present) {
                report.complete = false;
            }
        }
    } else {
        const SessionKeeper keeper(folder);
        plynth::UnpackOptions unpack;
        unpack.keepLibrary = options.keepLibrary;
        report = unpackProject(folder, opened.paths, unpack, stopToken(context));
    }
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Json result = unpackReportToJson(report);
    result["checked_only"] = options.check;
    Answer answer = success(command, std::move(result));
    for (const UnpackEntry& entry : report.entries) {
        if (entry.action == UnpackAction::NeedsDownload) {
            answer.warnings.push_back({"needs_download", "Get " + entry.entry.id + " on this machine: " + entry.command});
        } else if (entry.action == UnpackAction::Missing) {
            answer.warnings.push_back({"item_missing", "The " + std::string(packKindName(entry.entry.kind)) + " " +
                                                           entry.entry.id + " is in neither the pack nor this library."});
        } else if (entry.action == UnpackAction::Conflict) {
            answer.warnings.push_back({"kept_library_item", "The library's own " + entry.entry.id + " was kept."});
        }
    }
    return answer;
}

}

std::vector<CommandSpec> packCommands() {
    CommandSpec pack;
    pack.words = {"pack"};
    pack.description = "Copy the library items the project uses into its pack folder, so it can move to another "
                       "machine. Store items, items of unknown origin or with a license that forbids sharing, and "
                       "generators are only named.";
    pack.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<PackOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_flag("--check", options->check, "Say what the pack would hold, and write nothing.");
        return [options](const RunContext& context) { return packCommand(context, *options); };
    };

    CommandSpec unpack;
    unpack.words = {"unpack"};
    unpack.description = "Copy the items of the project's pack into this machine's library. Never overwrites and "
                         "never downloads.";
    unpack.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<UnpackCliOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_flag("--check", options->check, "Say what unpack would do, and write nothing.");
        sub.add_flag("--keep-library", options->keepLibrary,
                     "When the library has a different item with the same id, keep the library's and go on.");
        return [options](const RunContext& context) { return unpackCommand(context, *options); };
    };
    return {pack, unpack};
}

}
