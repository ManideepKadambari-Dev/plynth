#include "../Common.h"

#include <plynth/core/Snapping.h>
#include <plynth/core/commands/WallCommands.h>

#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct AddOptions {
    std::string project;
    std::string from;
    std::string to;
    double thickness = kDefaultWallThickness;
    std::optional<double> height;
};

// One end of a wall to draw: a point id, or a position.
struct EndOptions {
    std::string id;
    std::optional<double> x;
    std::optional<double> y;
};

struct DrawOptions {
    std::string project;
    std::string floor;
    EndOptions from;
    EndOptions to;
    double snap = 0.0;
    bool grid = false;
    double thickness = kDefaultWallThickness;
    std::optional<double> height;
};

struct SplitOptions {
    std::string project;
    std::string id;
    double offset = 0.0;
};

struct SetOptions {
    std::string project;
    std::string id;
    double thickness = 0.0;
};

struct RemoveOptions {
    bool cascade = false;
    std::string project;
    std::string id;
};

struct ListOptions {
    std::string project;
    std::string floor;
};

// The wall entry for an id, read from the changed plan.
Json entryFor(const Plan& plan, const Id& id) {
    const Floor* floor = findFloorContaining(plan, id);
    return wallEntry(*floor, *findWall(*floor, id));
}

// Adds a wall between two points.
Answer wallAdd(const RunContext& context, const AddOptions& options) {
    return edit(
        "wall add", context, options.project,
        [&](const Plan&) {
            return std::make_unique<AddWallCommand>(options.from, options.to, options.thickness, options.height);
        },
        [&](const Plan& plan, const AddWallCommand& added) { return entryFor(plan, added.wallId()); });
}

// Says whether an end was given as exactly one of an id or a full position.
bool endIsWellFormed(const EndOptions& end) {
    const bool hasId = !end.id.empty();
    const bool hasPosition = end.x.has_value() || end.y.has_value();
    if (hasId) {
        return !hasPosition;
    }
    return end.x.has_value() && end.y.has_value();
}

// Turns an end into a wall end the way the app does: a point id is used as it is, a position is snapped
// first and then read with wallEndFromSnap.
WallEnd makeEnd(const Floor& floor, const EndOptions& end, const SnapOptions& snapOptions) {
    if (!end.id.empty()) {
        return WallEnd::atPoint(end.id);
    }
    return wallEndFromSnap(snapPosition(floor, {*end.x, *end.y}, snapOptions));
}

// Draws one wall, snapping its ends, in a single step.
Answer wallDraw(const RunContext& context, const DrawOptions& options) {
    const std::string name = "wall draw";
    if (!endIsWellFormed(options.from)) {
        return badArguments(name, "bad_arguments", "Give the start as --from <point id> or as --from-x and --from-y.");
    }
    if (!endIsWellFormed(options.to)) {
        return badArguments(name, "bad_arguments", "Give the end as --to <point id> or as --to-x and --to-y.");
    }
    return editProject(name, context, options.project, [&](Document& document) {
        Answer failed;
        const Floor* floor = resolveFloor(name, document.plan(), options.floor, failed);
        if (floor == nullptr) {
            return failed;
        }
        SnapOptions snapOptions;
        snapOptions.radius = options.snap;
        snapOptions.gridSize = options.grid ? document.project().settings.gridSize : 0.0;
        const WallEnd from = makeEnd(*floor, options.from, snapOptions);
        const WallEnd to = makeEnd(*floor, options.to, snapOptions);

        auto command = std::make_unique<DrawWallCommand>(floor->id, from, to, options.thickness, options.height);
        const DrawWallCommand* raw = command.get();
        if (!runCommand(name, document, std::move(command), failed)) {
            return failed;
        }
        Json result = entryFor(document.plan(), raw->wallId());
        result["new_points"] = raw->newPointIds();
        result["split_walls"] = raw->splitWallIds();
        return success(name, std::move(result));
    });
}

// Splits a wall in two at a distance from its first point.
Answer wallSplit(const RunContext& context, const SplitOptions& options) {
    return edit(
        "wall split", context, options.project,
        [&](const Plan&) { return std::make_unique<SplitWallCommand>(options.id, options.offset); },
        [&](const Plan& plan, const SplitWallCommand& split) {
            const Floor* floor = findFloorContaining(plan, split.newPointId());
            Json walls = Json::array();
            walls.push_back(wallEntry(*floor, *findWall(*floor, options.id)));
            walls.push_back(wallEntry(*floor, *findWall(*floor, split.newWallId())));
            Json result = Json::object();
            result["point"] = pointEntry(*floor, *findPoint(*floor, split.newPointId()));
            result["walls"] = std::move(walls);
            return result;
        });
}

// Changes the thickness of a wall.
Answer wallSet(const RunContext& context, const SetOptions& options) {
    return edit(
        "wall set", context, options.project,
        [&](const Plan&) { return std::make_unique<SetWallThicknessCommand>(options.id, options.thickness); },
        [&](const Plan& plan, const SetWallThicknessCommand&) { return entryFor(plan, options.id); });
}

// Removes a wall that no room or opening uses.
Answer wallRemove(const RunContext& context, const RemoveOptions& options) {
    if (options.cascade) {
        return removeCascade("wall remove", context, options.project, options.id);
    }
    return edit(
        "wall remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveWallCommand>(options.id); },
        [&](const Plan&, const RemoveWallCommand&) { return removedJson({}, {options.id}, {}); });
}

// Lists the walls of a floor.
Answer wallList(const RunContext& context, const ListOptions& options) {
    const std::string command = "wall list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    Json walls = Json::array();
    for (const Wall& wall : floor->walls) {
        walls.push_back(wallEntry(*floor, wall));
    }
    Json result = Json::object();
    result["floor"] = floor->id;
    result["walls"] = std::move(walls);
    return success(command, std::move(result));
}

// Adds the options of one end of wall draw, with the given prefix ("from" or "to").
void addEndOptions(CLI::App& sub, EndOptions& end, const std::string& prefix, const std::string& what) {
    sub.add_option("--" + prefix, end.id, "Id of the point to use as the " + what + ".")->type_name("id");
    sub.add_option("--" + prefix + "-x", end.x, "Position to the right of the " + what + ", in metres.")
        ->type_name("metres");
    sub.add_option("--" + prefix + "-y", end.y, "Position down of the " + what + ", in metres.")
        ->type_name("metres");
}

}

std::vector<CommandSpec> wallCommands() {
    CommandSpec add;
    add.words = {"wall", "add"};
    add.description = "Add a straight wall between two points.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AddOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--from", options->from, "Id of the first point.")->type_name("id")->required();
        sub.add_option("--to", options->to, "Id of the second point.")->type_name("id")->required();
        sub.add_option("--thickness", options->thickness, "Wall thickness in metres. Default: 0.2.")
            ->type_name("metres");
        sub.add_option("--height", options->height, "Wall height in metres. Default: the floor's ceiling height.")
            ->type_name("metres");
        return [options](const RunContext& context) { return wallAdd(context, *options); };
    };

    CommandSpec draw;
    draw.words = {"wall", "draw"};
    draw.description = "Draw a wall between two places, snapping to points and walls like the app does.";
    draw.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<DrawOptions>();
        addProjectOption(sub, options->project);
        addEndOptions(sub, options->from, "from", "start");
        addEndOptions(sub, options->to, "to", "end");
        sub.add_option("--snap", options->snap,
                       "A position this close to a point or a wall snaps to it, in metres. Default: 0 (exact).")
            ->type_name("metres");
        sub.add_flag("--grid", options->grid, "Snap new positions to the project grid.");
        sub.add_option("--thickness", options->thickness, "Wall thickness in metres. Default: 0.2.")
            ->type_name("metres");
        sub.add_option("--height", options->height, "Wall height in metres. Default: the floor's ceiling height.")
            ->type_name("metres");
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return wallDraw(context, *options); };
    };

    CommandSpec split;
    split.words = {"wall", "split"};
    split.description = "Split a straight wall in two at a distance from its first point.";
    split.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SplitOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the wall.")->type_name("id")->required();
        sub.add_option("--offset", options->offset, "Distance from the wall's first point, in metres.")
            ->type_name("metres")
            ->required();
        return [options](const RunContext& context) { return wallSplit(context, *options); };
    };

    CommandSpec set;
    set.words = {"wall", "set"};
    set.description = "Change a wall.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the wall.")->type_name("id")->required();
        sub.add_option("--thickness", options->thickness, "New wall thickness in metres.")
            ->type_name("metres")
            ->required();
        return [options](const RunContext& context) { return wallSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"wall", "remove"};
    remove.description = "Remove a wall that no room or opening uses.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_flag("--cascade", options->cascade, "Also remove the rooms that depend on it, and end points left loose.");
        sub.add_option("id", options->id, "Id of the wall.")->type_name("id")->required();
        return [options](const RunContext& context) { return wallRemove(context, *options); };
    };

    CommandSpec list;
    list.words = {"wall", "list"};
    list.description = "List the walls of a floor.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return wallList(context, *options); };
    };

    return {add, draw, split, set, remove, list};
}

}
