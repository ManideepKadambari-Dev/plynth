#include "../Common.h"

#include <plynth/core/Blender.h>
#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>

#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct SetOptions {
    std::string library;
    std::string projects;
    std::string blender;
    std::string keepZips;
    std::string updateCheck;
    long long triangleWarning = 0;
    bool hasLibrary = false;
    bool hasProjects = false;
    bool hasBlender = false;
    bool hasKeepZips = false;
    bool hasUpdateCheck = false;
    bool hasTriangleWarning = false;
};

// The settings as printed by settings show and settings set.
Answer settingsAnswer(const std::string& command, const MachineSettings& settings, bool exists) {
    Json shown = settingsToJson(settings);
    shown.erase("format");
    shown.erase("version");
    Json result = Json::object();
    result["file"] = pathToUtf8(settingsFile());
    result["exists"] = exists;
    result["settings"] = std::move(shown);
    return success(command, std::move(result));
}

// An absolute path for a folder or file typed by the user, relative to the current folder.
std::filesystem::path absolutePath(const RunContext& context, const std::string& text) {
    return (context.currentFolder / pathFromUtf8(text)).lexically_normal();
}

// Shows the settings file, or the defaults when there is none yet.
Answer settingsShow() {
    const std::string command = "settings show";
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    return settingsAnswer(command, loaded.settings, loaded.exists);
}

// Checks every requested change before anything is saved, then saves them together.
Answer settingsSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "settings set";
    if (!options.hasLibrary && !options.hasProjects && !options.hasBlender &&
        !options.hasKeepZips && !options.hasUpdateCheck && !options.hasTriangleWarning) {
        return badArguments(command, "bad_arguments", "Give at least one option to change. Run 'plynth help'.");
    }
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    MachineSettings settings = loaded.settings;

    if (options.hasLibrary) {
        if (options.library == "none") {
            settings.library.reset();
        } else {
            const std::filesystem::path folder = absolutePath(context, options.library);
            if (!isLibraryFolder(folder)) {
                return failure(command, fail(ErrorCode::NoLibrary,
                                             pathToUtf8(folder) + " is not a library folder. Make one with "
                                             "'plynth library init <folder>'."));
            }
            settings.library = folder;
        }
    }
    if (options.hasProjects) {
        if (options.projects == "none") {
            settings.projects.reset();
        } else {
            const std::filesystem::path folder = absolutePath(context, options.projects);
            std::error_code ignored;
            if (!std::filesystem::is_directory(folder, ignored)) {
                return failure(command, fail(ErrorCode::NotFound, "The projects folder " + pathToUtf8(folder) +
                                                                      " does not exist."));
            }
            settings.projects = folder;
        }
    }
    if (options.hasBlender) {
        if (options.blender == "none") {
            settings.blender.reset();
        } else {
            const std::filesystem::path file = absolutePath(context, options.blender);
            std::error_code ignored;
            if (!std::filesystem::is_regular_file(file, ignored)) {
                return failure(command, fail(ErrorCode::BlenderNotFound,
                                             "There is no program at " + pathToUtf8(file) + "."));
            }
            const BlenderInstall install = inspectBlender(file);
            if (!install.usable()) {
                std::string why = install.status.ok() ? "it is older than Blender " +
                                                            formatBlenderVersion(kMinBlenderVersion)
                                                      : install.status.message;
                return failure(command, fail(ErrorCode::BlenderNotFound, pathToUtf8(file) + " cannot be used: " + why));
            }
            settings.blender = file;
        }
    }
    if (options.hasKeepZips) {
        settings.keepImportedZips = options.keepZips == "true";
    }
    if (options.hasUpdateCheck) {
        settings.updateCheck = options.updateCheck == "true";
    }
    if (options.hasTriangleWarning) {
        if (options.triangleWarning < 1) {
            return failure(command, fail(ErrorCode::InvalidValue, "The triangle warning must be at least 1."));
        }
        settings.triangleWarning = options.triangleWarning;
    }

    const Status saved = saveSettings(settings);
    if (!saved.ok()) {
        return failure(command, saved);
    }
    return settingsAnswer(command, settings, true);
}

}

std::vector<CommandSpec> settingsCommands() {
    CommandSpec show;
    show.words = {"settings", "show"};
    show.description = "Show the machine settings: library, projects folder, Blender and import options.";
    show.setup = [](CLI::App&) -> Handler {
        return [](const RunContext&) { return settingsShow(); };
    };

    CommandSpec set;
    set.words = {"settings", "set"};
    set.description = "Change machine settings. Folders and Blender are checked before anything is saved.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        sub.add_option("--library", options->library, "Library folder, or none to clear it.")
            ->type_name("folder|none")
            ->each([options](const std::string&) { options->hasLibrary = true; });
        sub.add_option("--projects", options->projects, "Where new projects go, or none to clear it.")
            ->type_name("folder|none")
            ->each([options](const std::string&) { options->hasProjects = true; });
        sub.add_option("--blender", options->blender, "The blender.exe to use, or none for the newest found.")
            ->type_name("file|none")
            ->each([options](const std::string&) { options->hasBlender = true; });
        sub.add_option("--keep-zips", options->keepZips, "Keep a zip after its asset was imported.")
            ->type_name("true|false")
            ->check(CLI::IsMember({"true", "false"}))
            ->each([options](const std::string&) { options->hasKeepZips = true; });
        sub.add_option("--update-check", options->updateCheck, "Look for a newer Plynth when the app starts.")
            ->type_name("true|false")
            ->check(CLI::IsMember({"true", "false"}))
            ->each([options](const std::string&) { options->hasUpdateCheck = true; });
        sub.add_option("--triangle-warning", options->triangleWarning,
                       "Imports with more triangles than this get a warning.")
            ->type_name("number")
            ->each([options](const std::string&) { options->hasTriangleWarning = true; });
        return [options](const RunContext& context) { return settingsSet(context, *options); };
    };

    return {show, set};
}

}
