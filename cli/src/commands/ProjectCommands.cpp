#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/PlanImage.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Settings.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/SettingsCommands.h>

#include <cmath>
#include <memory>
#include <optional>

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

// One recent project as project recent prints it.
Json recentEntry(const RecentProject& recent) {
    std::error_code ignored;
    Json entry = Json::object();
    entry["path"] = pathToUtf8(recent.path);
    entry["name"] = recent.name;
    entry["last_opened"] = formatSessionTime(recent.lastOpened);
    entry["exists"] = std::filesystem::is_directory(recent.path, ignored);
    return entry;
}

// Lists the projects the app opened or saved lately, newest first, or takes one off the list. The CLI
// never adds to the list; only the app does. Nothing on disk is touched.
Answer projectRecent(const RunContext& context, const std::string& removePath) {
    const std::string command = "project recent";
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    MachineSettings settings = loaded.settings;
    Json result = Json::object();
    if (!removePath.empty()) {
        const std::filesystem::path folder = userPath(context, removePath);
        if (!removeRecentProject(settings, folder)) {
            return failure(command, fail(ErrorCode::NotFound, "There is no recent project " + pathToUtf8(folder) + "."));
        }
        const Status saved = saveSettings(settings);
        if (!saved.ok()) {
            return failure(command, saved);
        }
        result["removed"] = pathToUtf8(folder);
    }
    Json projects = Json::array();
    for (const RecentProject& recent : settings.recentProjects) {
        projects.push_back(recentEntry(recent));
    }
    const std::size_t count = projects.size();
    result["projects"] = std::move(projects);
    result["count"] = count;
    return success(command, std::move(result));
}

struct SetOptions {
    std::string project;
    std::optional<double> grid;
    std::optional<double> doorWidth;
    std::optional<double> doorHeight;
    std::optional<double> windowWidth;
    std::optional<double> windowHeight;
    std::optional<double> windowSill;
    std::optional<double> newFloorHeight;
    std::optional<double> wallItemCentre;
};

// Changes the grid size and the default sizes in one step. Nothing changes when any value is refused.
Answer projectSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "project set";
    const bool anyDefault = options.doorWidth || options.doorHeight || options.windowWidth || options.windowHeight ||
                            options.windowSill || options.newFloorHeight || options.wallItemCentre;
    if (!options.grid && !anyDefault) {
        return badArguments(command, "bad_arguments", "Give at least one option to change. Run 'plynth help' to list them.");
    }
    return editProject(command, context, options.project, [&](Document& document) {
        auto group = std::make_unique<CommandGroup>("Change settings");
        if (options.grid) {
            group->add(std::make_unique<SetGridSizeCommand>(*options.grid));
        }
        if (anyDefault) {
            DefaultSizes sizes = document.project().settings.defaults;
            sizes.doorWidth = options.doorWidth.value_or(sizes.doorWidth);
            sizes.doorHeight = options.doorHeight.value_or(sizes.doorHeight);
            sizes.windowWidth = options.windowWidth.value_or(sizes.windowWidth);
            sizes.windowHeight = options.windowHeight.value_or(sizes.windowHeight);
            sizes.windowSill = options.windowSill.value_or(sizes.windowSill);
            sizes.ceilingHeight = options.newFloorHeight.value_or(sizes.ceilingHeight);
            sizes.wallItemCentre = options.wallItemCentre.value_or(sizes.wallItemCentre);
            group->add(std::make_unique<SetDefaultSizesCommand>(sizes));
        }
        Answer failed;
        if (!runCommand(command, document, std::move(group), failed)) {
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
    set.description = "Change the grid size or the default sizes of new openings, floors and wall items.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--grid", options->grid, "Grid size in metres.")->type_name("metres");
        sub.add_option("--door-width", options->doorWidth, "Default door width in metres.")->type_name("metres");
        sub.add_option("--door-height", options->doorHeight, "Default door height in metres.")->type_name("metres");
        sub.add_option("--window-width", options->windowWidth, "Default window width in metres.")
            ->type_name("metres");
        sub.add_option("--window-height", options->windowHeight, "Default window height in metres.")
            ->type_name("metres");
        sub.add_option("--window-sill", options->windowSill, "Default window sill height in metres.")
            ->type_name("metres");
        sub.add_option("--new-floor-height", options->newFloorHeight,
                       "Ceiling height for floors made later, in metres.")
            ->type_name("metres");
        sub.add_option("--wall-item-centre", options->wallItemCentre,
                       "How high the centre of a newly mounted wall item goes, in metres.")
            ->type_name("metres");
        return [options](const RunContext& context) { return projectSet(context, *options); };
    };

    CommandSpec recent;
    recent.words = {"project", "recent"};
    recent.description = "List the recent projects of the app, or take one off the list with --remove.";
    recent.setup = [](CLI::App& sub) -> Handler {
        auto remove = std::make_shared<std::string>();
        sub.add_option("--remove", *remove, "Take this project folder off the list. The folder itself stays.")
            ->type_name("folder");
        return [remove](const RunContext& context) { return projectRecent(context, *remove); };
    };

    return {create, info, set, recent};
}

}
