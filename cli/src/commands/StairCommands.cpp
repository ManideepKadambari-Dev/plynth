#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/Stairs.h>
#include <plynth/core/commands/StairCommands.h>

#include <cmath>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct EditOptions {
    std::string project;
    std::string library;
    std::string id;
    std::string floor;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> rotation;
    std::string type;
    std::optional<double> width;
    std::string turn;
    std::string material; // an id, or none
};

struct RemoveOptions {
    std::string project;
    std::string id;
};

struct ListOptions {
    std::string project;
    std::string floor;
};

// Rounds to a tenth of a millimetre so answers do not carry floating point noise.
double rounded(double value) {
    return std::round(value * 1e4) / 1e4;
}

// A plan position as [x, y].
Json pointJson(const Vec2& point) {
    Json json = Json::array();
    json.push_back(rounded(point.x));
    json.push_back(rounded(point.y));
    return json;
}

// The stair entry shared by the stair commands: values, the heights worked out from the floors, the footprint
// on the plan and the problems the build would report.
Json stairEntry(const Plan& plan, const Floor& floor, const Stair& stair) {
    const StairLayout layout = stairLayout(plan, floor, stair);
    Json entry = Json::object();
    entry["id"] = stair.id;
    entry["floor"] = floor.id;
    if (layout.status.ok()) {
        entry["to_floor"] = layout.toFloor;
    } else {
        entry["to_floor"] = nullptr;
    }
    entry["type"] = stair.type;
    entry["x"] = stair.x;
    entry["y"] = stair.y;
    entry["rotation"] = stair.rotation;
    entry["width"] = stair.width;
    entry["turn"] = std::string(stairTurnName(stair.turn));
    if (stair.material) {
        entry["material"] = *stair.material;
    } else {
        entry["material"] = nullptr;
    }
    entry["rise"] = rounded(layout.rise);
    entry["riser_count"] = layout.riserCount;
    entry["riser"] = std::round(layout.riser * 1e5) / 1e5;
    entry["going"] = layout.going;
    entry["length"] = rounded(layout.length);
    Json footprint = Json::array();
    for (const Vec2& point : layout.footprint) {
        footprint.push_back(pointJson(point));
    }
    entry["footprint"] = std::move(footprint);
    if (layout.status.ok()) {
        entry["arrival"] = pointJson(layout.arrival);
    } else {
        entry["arrival"] = nullptr;
    }
    Json problems = Json::array();
    for (const StairProblem& problem : stairProblems(plan, floor)) {
        if (problem.stair != stair.id) {
            continue;
        }
        Json item = Json::object();
        item["code"] = problem.code;
        item["severity"] = problem.error ? "error" : "warning";
        item["message"] = problem.message;
        problems.push_back(std::move(item));
    }
    entry["problems"] = std::move(problems);
    return entry;
}

// The stair entry of a stair that is in the plan.
Json stairEntryById(const Plan& plan, const Id& id) {
    for (const Floor& floor : plan.floors) {
        if (const Stair* stair = findStair(floor, id)) {
            return stairEntry(plan, floor, *stair);
        }
    }
    return Json::object();
}

// Checks a material id against the library, as material set does. Not used for none.
bool checkMaterial(const std::string& command, const RunContext& context, const EditOptions& options, Answer& failed) {
    if (options.material.empty() || options.material == "none") {
        return true;
    }
    if (!checkIdArgument(command, options.material, failed)) {
        return false;
    }
    OpenedMaterialCatalog opened;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return false;
    }
    const MaterialForUse usable = materialForUse(*opened.catalog, options.material);
    if (!usable.status.ok()) {
        failed = failure(command, usable.status);
        return false;
    }
    return true;
}

// Puts the options that were given into a stair.
void applyOptions(const EditOptions& options, Stair& stair) {
    if (options.x) {
        stair.x = *options.x;
    }
    if (options.y) {
        stair.y = *options.y;
    }
    if (options.rotation) {
        stair.rotation = *options.rotation;
    }
    if (!options.type.empty()) {
        stair.type = options.type;
    }
    if (options.width) {
        stair.width = *options.width;
    }
    if (!options.turn.empty()) {
        stair.turn = stairTurnFromName(options.turn).value_or(stair.turn);
    }
    if (options.material == "none") {
        stair.material.reset();
    } else if (!options.material.empty()) {
        stair.material = options.material;
    }
}

// Adds a stair to a floor. The stair goes up to the next floor.
Answer stairAdd(const RunContext& context, const EditOptions& options) {
    const std::string command = "stair add";
    Answer refused;
    if (!checkMaterial(command, context, options, refused)) {
        return refused;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
        if (floor == nullptr) {
            return failed;
        }
        const Id floorId = floor->id;
        Stair stair;
        applyOptions(options, stair);
        auto add = std::make_unique<AddStairCommand>(floorId, stair);
        const AddStairCommand* raw = add.get();
        if (!runCommand(command, document, std::move(add), failed)) {
            return failed;
        }
        return success(command, stairEntryById(document.plan(), raw->newStairId()));
    });
}

// Changes the values of a stair that were given.
Answer stairSet(const RunContext& context, const EditOptions& options) {
    const std::string command = "stair set";
    if (!options.x && !options.y && !options.rotation && options.type.empty() && !options.width &&
        options.turn.empty() && options.material.empty()) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --x, --y, --rot, --type, --width, --turn and --material.");
    }
    Answer refused;
    if (!checkMaterial(command, context, options, refused)) {
        return refused;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Stair* current = nullptr;
        for (const Floor& floor : document.plan().floors) {
            if (const Stair* found = findStair(floor, options.id)) {
                current = found;
            }
        }
        if (current == nullptr) {
            return failure(command, fail(ErrorCode::NotFound, "There is no stair " + options.id + "."));
        }
        Stair values = *current;
        applyOptions(options, values);
        if (!runCommand(command, document, std::make_unique<SetStairCommand>(options.id, values), failed)) {
            return failed;
        }
        return success(command, stairEntryById(document.plan(), options.id));
    });
}

// Removes a stair.
Answer stairRemove(const RunContext& context, const RemoveOptions& options) {
    return edit(
        "stair remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveStairCommand>(options.id); },
        [&](const Plan&, const RemoveStairCommand&) {
            Json result = Json::object();
            result["removed"] = options.id;
            return result;
        });
}

// Lists the stairs of one floor or of every floor.
Answer stairList(const RunContext& context, const ListOptions& options) {
    const std::string command = "stair list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Plan& plan = document.plan();
    if (!options.floor.empty() && findFloor(plan, options.floor) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no floor " + options.floor + "."));
    }
    Json stairs = Json::array();
    for (const Floor& floor : plan.floors) {
        if (!options.floor.empty() && floor.id != options.floor) {
            continue;
        }
        for (const Stair& stair : floor.stairs) {
            stairs.push_back(stairEntry(plan, floor, stair));
        }
    }
    Json result = Json::object();
    result["stairs"] = std::move(stairs);
    return success(command, std::move(result));
}

// Lists the stair types.
Answer stairTypes() {
    Json types = Json::array();
    for (const StairType* type : allStairTypes()) {
        Json item = Json::object();
        item["id"] = std::string(type->id());
        item["name"] = std::string(type->name());
        item["uses_turn"] = type->usesTurn();
        types.push_back(std::move(item));
    }
    Json result = Json::object();
    result["types"] = std::move(types);
    return success("stair types", std::move(result));
}

// The options stair add and stair set share.
void addEditOptions(CLI::App& sub, EditOptions& options) {
    addProjectOption(sub, options.project);
    addLibraryOption(sub, options.library);
    sub.add_option("--type", options.type, "Stair type. See stair types. Default: straight.")->type_name("id");
    sub.add_option("--rot", options.rotation, "Direction of the climb in degrees, clockwise, 0 is down the plan.")
        ->type_name("degrees");
    sub.add_option("--width", options.width, "Width of the stair in metres.")->type_name("metres");
    sub.add_option("--turn", options.turn, "Which way an L, U or spiral stair turns.")
        ->type_name("left|right")
        ->check(CLI::IsMember({"left", "right"}));
    sub.add_option("--material", options.material, "Material id for the steps, or none.")->type_name("id");
}

}

std::vector<CommandSpec> stairCommands() {
    CommandSpec add;
    add.words = {"stair", "add"};
    add.description = "Add a stair to a floor. It climbs to the next floor.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<EditOptions>();
        sub.add_option("--floor", options->floor, "Floor the stair starts on. Default: the first floor.")
            ->type_name("id");
        sub.add_option("--x", options->x, "Position of the bottom step's front edge, metres.")
            ->type_name("metres")
            ->required();
        sub.add_option("--y", options->y, "Position of the bottom step's front edge, metres.")
            ->type_name("metres")
            ->required();
        addEditOptions(sub, *options);
        return [options](const RunContext& context) { return stairAdd(context, *options); };
    };
    CommandSpec set;
    set.words = {"stair", "set"};
    set.description = "Change the position, type, width, turn or material of a stair.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<EditOptions>();
        sub.add_option("id", options->id, "Stair id.")->type_name("id")->required();
        sub.add_option("--x", options->x, "Position of the bottom step's front edge, metres.")->type_name("metres");
        sub.add_option("--y", options->y, "Position of the bottom step's front edge, metres.")->type_name("metres");
        addEditOptions(sub, *options);
        return [options](const RunContext& context) { return stairSet(context, *options); };
    };
    CommandSpec remove;
    remove.words = {"stair", "remove"};
    remove.description = "Remove a stair.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Stair id.")->type_name("id")->required();
        return [options](const RunContext& context) { return stairRemove(context, *options); };
    };
    CommandSpec list;
    list.words = {"stair", "list"};
    list.description = "List the stairs with their steps, footprint and problems.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Only the stairs of this floor.")->type_name("id");
        return [options](const RunContext& context) { return stairList(context, *options); };
    };
    CommandSpec types;
    types.words = {"stair", "types"};
    types.description = "List the stair types.";
    types.setup = [](CLI::App&) -> Handler {
        return [](const RunContext&) { return stairTypes(); };
    };

    return {add, set, remove, list, types};
}

}
