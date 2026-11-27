#include "../Common.h"

#include <plynth/core/FinishSettings.h>
#include <plynth/core/LightStrips.h>
#include <plynth/core/commands/LightStripCommands.h>

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth::cli {

namespace {

struct AddOptions {
    std::string project;
    std::string floor;
    std::string kind;
    std::string points;
    std::optional<double> z;
    std::string stair;
    std::optional<double> power;
};

struct SetOptions {
    std::string project;
    std::string id;
    std::string kind;
    std::string points;
    std::optional<double> z;
    std::string stair; // an id, or none
    std::optional<double> power;
};

struct RemoveOptions {
    std::string project;
    std::string id;
};

struct ListOptions {
    std::string project;
    std::string floor;
};

// The light strip entry: id, floor, kind, its points, height, stair (or null) and power per metre.
Json stripEntry(const Floor& floor, const LightStrip& strip) {
    Json entry = Json::object();
    entry["id"] = strip.id;
    entry["floor"] = floor.id;
    entry["kind"] = std::string(lightStripKindName(strip.kind));
    Json points = Json::array();
    for (const PlanXY& point : strip.points) {
        Json one = Json::object();
        one["x"] = point.x;
        one["y"] = point.y;
        points.push_back(std::move(one));
    }
    entry["points"] = std::move(points);
    entry["z"] = strip.z;
    entry["stair"] = strip.stair ? Json(*strip.stair) : Json(nullptr);
    entry["power"] = strip.power;
    return entry;
}

// The light strip entry for an id, read from the changed plan.
Json entryFor(const Plan& plan, const Id& id) {
    const Floor* floor = findFloorContaining(plan, id);
    return stripEntry(*floor, *findLightStrip(*floor, id));
}

// The kind named by --kind, or nothing with failed filled (invalid_value) when the name is unknown.
std::optional<LightStripKind> parseKind(const std::string& command, const std::string& name, Answer& failed) {
    const std::optional<LightStripKind> kind = lightStripKindFromName(name);
    if (!kind) {
        failed = failure(command, fail(ErrorCode::InvalidValue, "The light strip kind '" + name +
                                                                    "' is unknown. Use cove, soffit, edge or step."));
    }
    return kind;
}

// Reads --points "x1,y1,x2,y2,...": numbers in pairs. Nothing, with failed filled (bad_arguments), when a value
// is not a number or the count is odd.
std::optional<std::vector<PlanXY>> parsePoints(const std::string& command, const std::string& text, Answer& failed) {
    std::vector<double> numbers;
    std::string_view rest = text;
    while (true) {
        const std::size_t comma = rest.find(',');
        const std::string_view part = rest.substr(0, comma);
        double value = 0.0;
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || error != std::errc() || end != part.data() + part.size()) {
            failed = badArguments(command, "bad_arguments",
                                  "--points takes numbers separated by commas, such as 1,3,5,3. '" +
                                      std::string(part) + "' is not a number.");
            return std::nullopt;
        }
        numbers.push_back(value);
        if (comma == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(comma + 1);
    }
    if (numbers.size() % 2 != 0) {
        failed = badArguments(command, "bad_arguments", "--points takes an x and a y for every point.");
        return std::nullopt;
    }
    std::vector<PlanXY> points;
    for (std::size_t i = 0; i < numbers.size(); i += 2) {
        points.push_back(PlanXY{numbers[i], numbers[i + 1]});
    }
    return points;
}

// Adds a light strip to a floor. Without --z it starts at the usual height for its kind.
Answer stripAdd(const RunContext& context, const AddOptions& options) {
    const std::string command = "light-strip add";
    Answer failed;
    const std::optional<LightStripKind> kind = parseKind(command, options.kind, failed);
    if (!kind) {
        return failed;
    }
    std::vector<PlanXY> points;
    if (!options.points.empty()) {
        const auto parsed = parsePoints(command, options.points, failed);
        if (!parsed) {
            return failed;
        }
        points = *parsed;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer refused;
        const Floor* floor = resolveFloor(command, document.plan(), options.floor, refused);
        if (floor == nullptr) {
            return refused;
        }
        LightStrip strip;
        strip.kind = *kind;
        strip.points = points;
        strip.z = options.z.value_or(defaultStripHeight(*kind, floor->ceilingHeight));
        if (!options.stair.empty()) {
            strip.stair = options.stair;
        }
        if (options.power) {
            strip.power = *options.power;
        }
        auto added = std::make_unique<AddLightStripCommand>(floor->id, strip);
        const AddLightStripCommand* raw = added.get();
        if (!runCommand(command, document, std::move(added), refused)) {
            return refused;
        }
        return success(command, entryFor(document.plan(), raw->newStripId()));
    });
}

// Changes a light strip's kind, points, height, stair or power, as one step. What is left out stays, except
// that a kind other than step drops the stair and kind step drops the points.
Answer stripSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "light-strip set";
    if (options.kind.empty() && options.points.empty() && !options.z && options.stair.empty() && !options.power) {
        return badArguments(command, "bad_arguments", "Give at least one of --kind, --points, --z, --stair and --power.");
    }
    Answer failed;
    std::optional<LightStripKind> kind;
    if (!options.kind.empty()) {
        kind = parseKind(command, options.kind, failed);
        if (!kind) {
            return failed;
        }
    }
    std::optional<std::vector<PlanXY>> points;
    if (!options.points.empty()) {
        points = parsePoints(command, options.points, failed);
        if (!points) {
            return failed;
        }
    }
    return editProject(command, context, options.project, [&](Document& document) {
        const Floor* floor = findFloorContaining(document.plan(), options.id);
        const LightStrip* current = floor == nullptr ? nullptr : findLightStrip(*floor, options.id);
        if (current == nullptr) {
            return failure(command, fail(ErrorCode::NotFound, "There is no light strip " + options.id + "."));
        }
        LightStrip values = *current;
        if (kind && *kind != values.kind) {
            values.kind = *kind;
            if (*kind == LightStripKind::Step) {
                values.points.clear();
            } else {
                values.stair.reset();
            }
        }
        if (points) {
            values.points = *points;
        }
        if (options.z) {
            values.z = *options.z;
        }
        if (options.stair == "none") {
            values.stair.reset();
        } else if (!options.stair.empty()) {
            values.stair = options.stair;
        }
        if (options.power) {
            values.power = *options.power;
        }
        Answer refused;
        if (!runCommand(command, document, std::make_unique<SetLightStripCommand>(options.id, values), refused)) {
            return refused;
        }
        return success(command, entryFor(document.plan(), options.id));
    });
}

// Removes a light strip.
Answer stripRemove(const RunContext& context, const RemoveOptions& options) {
    return edit(
        "light-strip remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveLightStripCommand>(options.id); },
        [&](const Plan&, const RemoveLightStripCommand&) {
            Json result = Json::object();
            result["removed"] = options.id;
            return result;
        });
}

// Lists the light strips of a floor.
Answer stripList(const RunContext& context, const ListOptions& options) {
    const std::string command = "light-strip list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    Json strips = Json::array();
    for (const LightStrip& strip : floor->lightStrips) {
        strips.push_back(stripEntry(*floor, strip));
    }
    Json result = Json::object();
    result["floor"] = floor->id;
    result["light_strips"] = std::move(strips);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> lightStripCommands() {
    CommandSpec add;
    add.words = {"light-strip", "add"};
    add.description = "Add a light strip along a cove, a soffit, an edge or the steps of a stair.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AddOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--kind", options->kind, "Strip kind.")->type_name("cove|soffit|edge|step")->required();
        sub.add_option("--points", options->points,
                       "The line of the strip, as x1,y1,x2,y2,... in metres. At least two points. Not for kind step.")
            ->type_name("x,y,...");
        sub.add_option("--z", options->z,
                       "Height above the floor, in metres, from 0 to the ceiling. Default: 0.05 below the ceiling "
                       "for cove, the ceiling for soffit, 0 for edge and step.")
            ->type_name("metres");
        sub.add_option("--stair", options->stair, "The stair a step strip lights. Only for kind step.")->type_name("id");
        sub.add_option("--power", options->power, "Watts per metre, from 0 to 200. Default: 10.")->type_name("watts");
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return stripAdd(context, *options); };
    };

    CommandSpec set;
    set.words = {"light-strip", "set"};
    set.description = "Change a light strip's kind, line, height, stair or power.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the light strip.")->type_name("id")->required();
        sub.add_option("--kind", options->kind,
                       "New kind. Changing to step drops the points, changing from step drops the stair.")
            ->type_name("cove|soffit|edge|step");
        sub.add_option("--points", options->points, "New line, as x1,y1,x2,y2,... in metres.")->type_name("x,y,...");
        sub.add_option("--z", options->z, "New height above the floor, in metres.")->type_name("metres");
        sub.add_option("--stair", options->stair, "The stair a step strip lights, or none.")->type_name("id");
        sub.add_option("--power", options->power, "New watts per metre, from 0 to 200.")->type_name("watts");
        return [options](const RunContext& context) { return stripSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"light-strip", "remove"};
    remove.description = "Remove a light strip.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the light strip.")->type_name("id")->required();
        return [options](const RunContext& context) { return stripRemove(context, *options); };
    };

    CommandSpec list;
    list.words = {"light-strip", "list"};
    list.description = "List the light strips of a floor.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return stripList(context, *options); };
    };

    return {add, set, remove, list};
}

}
