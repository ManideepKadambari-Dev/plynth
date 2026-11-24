#include "../Common.h"

#include <plynth/core/FinishSettings.h>
#include <plynth/core/commands/LightCommands.h>

#include <memory>
#include <optional>
#include <string>

namespace plynth::cli {

namespace {

struct AddOptions {
    std::string project;
    std::string floor;
    double x = 0.0;
    double y = 0.0;
    std::optional<double> z;
    std::string kind = "ceiling";
};

struct SetOptions {
    std::string project;
    std::string id;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    std::string kind;
};

struct RemoveOptions {
    std::string project;
    std::string id;
};

struct ListOptions {
    std::string project;
    std::string floor;
};

// The light entry: id, floor, kind, the room it stands in (or null) and its position.
Json lightEntry(const Floor& floor, const LightMarker& light) {
    Json entry = Json::object();
    entry["id"] = light.id;
    entry["floor"] = floor.id;
    entry["kind"] = std::string(lightKindName(light.kind));
    entry["room"] = light.room ? Json(*light.room) : Json(nullptr);
    entry["x"] = light.x;
    entry["y"] = light.y;
    entry["z"] = light.z;
    return entry;
}

// The light entry for an id, read from the changed plan.
Json entryFor(const Plan& plan, const Id& id) {
    const Floor* floor = findFloorContaining(plan, id);
    return lightEntry(*floor, *findLight(*floor, id));
}

// The kind named by --kind, or nothing with failed filled (invalid_value) when the name is unknown.
std::optional<LightKind> parseKind(const std::string& command, const std::string& name, Answer& failed) {
    const std::optional<LightKind> kind = lightKindFromName(name);
    if (!kind) {
        failed = failure(command, fail(ErrorCode::InvalidValue,
                                       "The light kind '" + name + "' is unknown. Use ceiling, wall or standing."));
    }
    return kind;
}

// Places a light marker on a floor. Without --z it starts at the usual height for its kind.
Answer lightAdd(const RunContext& context, const AddOptions& options) {
    const std::string command = "light add";
    Answer failed;
    const std::optional<LightKind> kind = parseKind(command, options.kind, failed);
    if (!kind) {
        return failed;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer refused;
        const Floor* floor = resolveFloor(command, document.plan(), options.floor, refused);
        if (floor == nullptr) {
            return refused;
        }
        LightMarker light;
        light.kind = *kind;
        light.x = options.x;
        light.y = options.y;
        light.z = options.z.value_or(defaultLightHeight(*kind, floor->ceilingHeight));
        auto added = std::make_unique<AddLightCommand>(floor->id, light);
        const AddLightCommand* raw = added.get();
        if (!runCommand(command, document, std::move(added), refused)) {
            return refused;
        }
        return success(command, entryFor(document.plan(), raw->newLightId()));
    });
}

// Moves a light or changes its kind or height, as one step. What is left out stays.
Answer lightSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "light set";
    if (!options.x && !options.y && !options.z && options.kind.empty()) {
        return badArguments(command, "bad_arguments", "Give at least one of --x, --y, --z and --kind.");
    }
    Answer failed;
    std::optional<LightKind> kind;
    if (!options.kind.empty()) {
        kind = parseKind(command, options.kind, failed);
        if (!kind) {
            return failed;
        }
    }
    return editProject(command, context, options.project, [&](Document& document) {
        const Floor* floor = findFloorContaining(document.plan(), options.id);
        const LightMarker* current = floor == nullptr ? nullptr : findLight(*floor, options.id);
        if (current == nullptr) {
            return failure(command, fail(ErrorCode::NotFound, "There is no light " + options.id + "."));
        }
        auto group = std::make_unique<CommandGroup>("Change light");
        if (options.x || options.y) {
            group->add(std::make_unique<MoveLightCommand>(options.id, options.x.value_or(current->x),
                                                          options.y.value_or(current->y)));
        }
        if (kind || options.z) {
            group->add(std::make_unique<SetLightCommand>(options.id, kind.value_or(current->kind),
                                                         options.z.value_or(current->z)));
        }
        Answer refused;
        if (!runCommand(command, document, std::move(group), refused)) {
            return refused;
        }
        return success(command, entryFor(document.plan(), options.id));
    });
}

// Removes a light marker.
Answer lightRemove(const RunContext& context, const RemoveOptions& options) {
    return edit(
        "light remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveLightCommand>(options.id); },
        [&](const Plan&, const RemoveLightCommand&) {
            Json result = Json::object();
            result["removed"] = options.id;
            return result;
        });
}

// Lists the light markers of a floor.
Answer lightList(const RunContext& context, const ListOptions& options) {
    const std::string command = "light list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    Json lights = Json::array();
    for (const LightMarker& light : floor->lights) {
        lights.push_back(lightEntry(*floor, light));
    }
    Json result = Json::object();
    result["floor"] = floor->id;
    result["lights"] = std::move(lights);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> lightCommands() {
    CommandSpec add;
    add.words = {"light", "add"};
    add.description = "Place a light marker, where the build puts a point light.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AddOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--x", options->x, "Position to the right, in metres.")->type_name("metres")->required();
        sub.add_option("--y", options->y, "Position down, in metres.")->type_name("metres")->required();
        sub.add_option("--z", options->z,
                       "Height above the floor, in metres, from 0 to the ceiling. Default: 0.1 below the ceiling "
                       "for a ceiling light, 1.8 for a wall light, 1.4 for a standing lamp.")
            ->type_name("metres");
        sub.add_option("--kind", options->kind, "Light kind. Default: ceiling.")->type_name("ceiling|wall|standing");
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return lightAdd(context, *options); };
    };

    CommandSpec set;
    set.words = {"light", "set"};
    set.description = "Move a light marker or change its kind or height.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the light.")->type_name("id")->required();
        sub.add_option("--x", options->x, "New position to the right, in metres.")->type_name("metres");
        sub.add_option("--y", options->y, "New position down, in metres.")->type_name("metres");
        sub.add_option("--z", options->z, "New height above the floor, in metres.")->type_name("metres");
        sub.add_option("--kind", options->kind, "New kind. The height stays.")->type_name("ceiling|wall|standing");
        return [options](const RunContext& context) { return lightSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"light", "remove"};
    remove.description = "Remove a light marker.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the light.")->type_name("id")->required();
        return [options](const RunContext& context) { return lightRemove(context, *options); };
    };

    CommandSpec list;
    list.words = {"light", "list"};
    list.description = "List the light markers of a floor.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return lightList(context, *options); };
    };

    return {add, set, remove, list};
}

}
