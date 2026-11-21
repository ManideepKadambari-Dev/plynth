#include "../Common.h"

#include <plynth/core/PlanImage.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/SettingsCommands.h>

#include <cmath>
#include <memory>

namespace plynth::cli {

namespace {

struct NewOptions {
    std::string project;
    std::string image;
    std::string exportMode = "single";
    double grid = kDefaultGridSize;
    std::string name;
};

// Makes a new project folder, with the plan image copied in when one is given.
Answer projectNew(const RunContext& context, const NewOptions& options) {
    const std::string command = "project new";
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);

    if (isProjectFolder(folder)) {
        return failure(command, fail(ErrorCode::AlreadyExists,
                                     "The folder " + pathToUtf8(folder) + " already holds a project."));
    }
    if (!std::isfinite(options.grid) || options.grid <= 0.0) {
        return failure(command, fail(ErrorCode::InvalidValue, "The grid size must be above zero."));
    }

    std::filesystem::path imagePath;
    if (!options.image.empty()) {
        imagePath = (context.currentFolder / pathFromUtf8(options.image)).lexically_normal();
        const PlanImageProbe probe = probePlanImage(imagePath);
        if (!probe.ok()) {
            return failure(command, fail(ErrorCode::ImageError, probe.message));
        }
    }

    Project project = makeNewProject();
    project.settings.name = options.name.empty() ? pathToUtf8(folder.filename()) : options.name;
    project.settings.exportMode = exportModeFromName(options.exportMode).value_or(ExportMode::Single);
    project.settings.gridSize = options.grid;

    Document document;
    document.replace(std::move(project), {});
    if (!imagePath.empty()) {
        const Status set = document.execute(std::make_unique<SetFloorImageCommand>(
            defaultFloorId(document.plan()), pathToUtf8(imagePath)));
        if (!set.ok()) {
            return failure(command, set);
        }
    }

    Project toSave = document.project();
    const Status saved = createProject(toSave, folder, {});
    if (!saved.ok()) {
        return failure(command, saved);
    }
    return success(command, projectInfoJson(folder, toSave));
}

// Reports what a project folder holds.
Answer projectInfo(const RunContext& context, const std::string& projectOption) {
    const std::string command = "project info";
    const std::filesystem::path folder = resolveProjectFolder(context, projectOption);
    Answer refused;
    if (!checkLock(command, folder, true, refused)) {
        return refused;
    }
    const ProjectLoad loaded = loadProject(folder);
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    return success(command, projectInfoJson(folder, loaded.project));
}

// Changes the project's settings. Only the grid size for now.
Answer projectSet(const RunContext& context, const std::string& projectOption, double grid) {
    const std::string command = "project set";
    return editProject(command, context, projectOption, [&](Document& document) {
        Answer failed;
        if (!runCommand(command, document, std::make_unique<SetGridSizeCommand>(grid), failed)) {
            return failed;
        }
        return success(command, projectInfoJson(document.folder(), document.project()));
    });
}

}

std::vector<CommandSpec> projectCommands() {
    CommandSpec create;
    create.words = {"project", "new"};
    create.description = "Create a project folder, optionally with a plan image.";
    create.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<NewOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--image", options->image, "Plan image to use for the first floor (PNG, JPEG or BMP).")
            ->type_name("file");
        sub.add_option("--export", options->exportMode, "Export mode. Default: single.")
            ->type_name("single|grouped")
            ->check(CLI::IsMember({"single", "grouped"}));
        sub.add_option("--grid", options->grid, "Grid size in metres. Default: 0.5.")->type_name("metres");
        sub.add_option("--name", options->name, "Project name. Default: the folder name.")->type_name("text");
        return [options](const RunContext& context) { return projectNew(context, *options); };
    };

    CommandSpec info;
    info.words = {"project", "info"};
    info.description = "Show the settings and contents of a project.";
    info.setup = [](CLI::App& sub) -> Handler {
        auto project = std::make_shared<std::string>();
        addProjectOption(sub, *project);
        return [project](const RunContext& context) { return projectInfo(context, *project); };
    };

    CommandSpec set;
    set.words = {"project", "set"};
    set.description = "Change a project setting.";
    set.setup = [](CLI::App& sub) -> Handler {
        struct SetOptions {
            std::string project;
            double grid = 0.0;
        };
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--grid", options->grid, "Grid size in metres.")->type_name("metres")->required();
        return [options](const RunContext& context) { return projectSet(context, options->project, options->grid); };
    };

    return {create, info, set};
}

}
