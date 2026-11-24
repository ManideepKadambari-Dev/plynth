#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/FinishSettings.h>
#include <plynth/core/TrimProfiles.h>
#include <plynth/core/commands/FinishCommands.h>

#include <memory>
#include <optional>
#include <string>

namespace plynth::cli {

namespace {

constexpr TrimKind kTrimKinds[] = {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill,
                                   TrimKind::Railing};

struct ShowOptions {
    std::string project;
};

struct ProfilesOptions {
    std::string library;
};

struct SetOptions {
    std::string project;
    std::string library;
    std::string kind;
    bool on = false;
    bool off = false;
    std::string profile;
    std::optional<double> height;
    std::optional<double> depth;
    std::optional<double> railingHeight;
};

// The trim answer: one entry per kind with its switch, profile and size, then the railing height.
Json trimJson(const TrimSettings& trim) {
    Json result = Json::object();
    for (const TrimKind kind : kTrimKinds) {
        const TrimStyle& style = trimStyle(trim, kind);
        Json entry = Json::object();
        entry["enabled"] = style.enabled;
        entry["profile"] = style.profile;
        entry["profile_found"] = findTrimProfile(style.profile) != nullptr;
        entry["height"] = style.height;
        entry["depth"] = style.depth;
        result[std::string(trimKindName(kind))] = std::move(entry);
    }
    result["railing_height"] = trim.railingHeight;
    return result;
}

// Shows the trim settings of the project.
Answer trimShow(const RunContext& context, const ShowOptions& options) {
    const std::string command = "trim show";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    registerLibraryShapes();
    return success(command, trimJson(document.project().settings.trim));
}

// Lists the trim profiles: the built-in ones and those in the library's shapes folder. Needs no project.
Answer trimProfiles(const RunContext& context, const ProfilesOptions& options) {
    const std::string command = "trim profiles";
    Answer failed;
    if (!registerShapesFor(command, context, options.library, failed)) {
        return failed;
    }
    Json profiles = Json::array();
    for (const TrimProfile* profile : allTrimProfiles()) {
        Json entry = Json::object();
        entry["id"] = std::string(profile->id());
        entry["name"] = std::string(profile->name());
        entry["built_in"] = profile->builtIn();
        profiles.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["profiles"] = std::move(profiles);
    return success(command, std::move(result));
}

// Changes one kind of trim. What is left out stays.
Answer trimSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "trim set";
    if (options.on && options.off) {
        return badArguments(command, "bad_arguments", "Give --on or --off, not both.");
    }
    if (!options.on && !options.off && options.profile.empty() && !options.height && !options.depth &&
        !options.railingHeight) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --on, --off, --profile, --height, --depth and --railing-height.");
    }
    const std::optional<TrimKind> kind = trimKindFromName(options.kind);
    if (!kind) {
        return failure(command, fail(ErrorCode::InvalidValue,
                                     "The trim kind '" + options.kind +
                                         "' is unknown. Use skirting, cornice, casing, sill or railing."));
    }
    if (options.railingHeight && *kind != TrimKind::Railing) {
        return badArguments(command, "bad_arguments", "--railing-height belongs to 'trim set railing'.");
    }
    Answer failed;
    if (!registerShapesFor(command, context, options.library, failed)) {
        return failed;
    }
    if (!options.profile.empty() && findTrimProfile(options.profile) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no trim profile " + options.profile +
                                                              ". Run 'plynth trim profiles' to list them."));
    }
    return editProject(command, context, options.project, [&](Document& document) {
        TrimSettings trim = document.project().settings.trim;
        TrimStyle& style = trimStyle(trim, *kind);
        if (options.on || options.off) {
            style.enabled = options.on;
        }
        if (!options.profile.empty()) {
            style.profile = options.profile;
        }
        style.height = options.height.value_or(style.height);
        style.depth = options.depth.value_or(style.depth);
        trim.railingHeight = options.railingHeight.value_or(trim.railingHeight);
        Answer refused;
        if (!runCommand(command, document, std::make_unique<SetTrimCommand>(trim), refused)) {
            return refused;
        }
        return success(command, trimJson(document.project().settings.trim));
    });
}

}

std::vector<CommandSpec> trimCommands() {
    CommandSpec show;
    show.words = {"trim", "show"};
    show.description = "Show the trim of the project: skirting boards, cornices, casings, sills and stair railings.";
    show.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ShowOptions>();
        addProjectOption(sub, options->project);
        return [options](const RunContext& context) { return trimShow(context, *options); };
    };

    CommandSpec profiles;
    profiles.words = {"trim", "profiles"};
    profiles.description = "List the trim profiles, built in and from the library.";
    profiles.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ProfilesOptions>();
        addLibraryOption(sub, options->library);
        return [options](const RunContext& context) { return trimProfiles(context, *options); };
    };

    CommandSpec set;
    set.words = {"trim", "set"};
    set.description = "Change one kind of trim: turn it on or off, or change its profile or size.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("kind", options->kind, "The kind of trim.")
            ->type_name("skirting|cornice|casing|sill|railing")
            ->required();
        sub.add_flag("--on", options->on, "Build this kind of trim.");
        sub.add_flag("--off", options->off, "Leave this kind of trim out.");
        sub.add_option("--profile", options->profile, "Profile id. Run 'trim profiles' to list them.")
            ->type_name("id");
        sub.add_option("--height", options->height,
                       "Size up the wall (across the wall face for a casing), in metres, from 0.005 to 0.5.")
            ->type_name("metres");
        sub.add_option("--depth", options->depth, "How far it stands out, in metres, from 0.005 to 0.5.")
            ->type_name("metres");
        sub.add_option("--railing-height", options->railingHeight,
                       "Height of the top of a stair railing above the steps, in metres, from 0.5 to 1.5. Only "
                       "with railing.")
            ->type_name("metres");
        addLibraryOption(sub, options->library);
        return [options](const RunContext& context) { return trimSet(context, *options); };
    };

    return {show, profiles, set};
}

}
