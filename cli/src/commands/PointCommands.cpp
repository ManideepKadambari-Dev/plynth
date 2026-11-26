#include "../Common.h"

#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct AddOptions {
    std::string project;
    std::string floor;
    double x = 0.0;
    double y = 0.0;
    double cornerRadius = 0.0;
};

struct MoveOptions {
    std::string project;
    std::string id;
    double x = 0.0;
    double y = 0.0;
};

struct JoinOptions {
    std::string project;
    std::string id;
    std::string to;
    std::string wall;
    std::optional<double> offset;
};

struct RemoveOptions {
    bool cascade = false;
    std::string project;
    std::string id;
};

struct SetOptions {
    std::string project;
    std::string id;
    double cornerRadius = 0.0;
};

struct ListOptions {
    std::string project;
    std::string floor;
};

// The point entry for an id, read from the changed plan.
Json entryFor(const Plan& plan, const Id& id) {
    const Floor* floor = findFloorContaining(plan, id);
    return pointEntry(*floor, *findPoint(*floor, id));
}

// Adds a point to a floor.
Answer pointAdd(const RunContext& context, const AddOptions& options) {
    return edit(
        "point add", context, options.project,
        [&](const Plan& plan) {
            const std::string floor = options.floor.empty() ? defaultFloorId(plan) : options.floor;
            return std::make_unique<AddPointCommand>(floor, options.x, options.y, options.cornerRadius);
        },
        [&](const Plan& plan, const AddPointCommand& added) { return entryFor(plan, added.pointId()); });
}

// Moves a point to a new place.
Answer pointMove(const RunContext& context, const MoveOptions& options) {
    return edit(
        "point move", context, options.project,
        [&](const Plan&) { return std::make_unique<MovePointCommand>(options.id, options.x, options.y); },
        [&](const Plan& plan, const MovePointCommand&) { return entryFor(plan, options.id); });
}

// Joins a point onto another point or a place on a wall, the way dropping a dragged point there does in the app.
Answer pointJoin(const RunContext& context, const JoinOptions& options) {
    const std::string name = "point join";
    const bool toPoint = !options.to.empty();
    const bool toWall = !options.wall.empty();
    if (toPoint == toWall) {
        return badArguments(name, "bad_arguments", "Give the target as --to <point id> or as --wall <wall id> with --offset.");
    }
    if (toWall != options.offset.has_value()) {
        return badArguments(name, "bad_arguments", "--wall and --offset go together.");
    }
    const WallEnd target = toPoint ? WallEnd::atPoint(options.to) : WallEnd::onWall(options.wall, *options.offset);
    return edit(
        name, context, options.project, [&](const Plan&) { return std::make_unique<JoinPointCommand>(options.id, target); },
        [&](const Plan& plan, const JoinPointCommand& join) {
            Json result = entryFor(plan, join.targetPointId());
            result["removed_point"] = options.id;
            result["new_rooms"] = join.newRoomIds();
            return result;
        });
}

// Rounds the corner at a point.
Answer pointSet(const RunContext& context, const SetOptions& options) {
    return edit(
        "point set", context, options.project,
        [&](const Plan&) { return std::make_unique<SetCornerRadiusCommand>(options.id, options.cornerRadius); },
        [&](const Plan& plan, const SetCornerRadiusCommand&) { return entryFor(plan, options.id); });
}

// Removes a point that no wall uses.
Answer pointRemove(const RunContext& context, const RemoveOptions& options) {
    if (options.cascade) {
        return removeCascade("point remove", "point", context, options.project, options.id);
    }
    if (auto refused = refuseStairId("point remove", "point", context, options.project, options.id)) {
        return *refused;
    }
    return edit(
        "point remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemovePointCommand>(options.id); },
        [&](const Plan&, const RemovePointCommand&) { return removedJson({}, {}, {}, {}, {}, {options.id}); });
}

// Lists the points of a floor.
Answer pointList(const RunContext& context, const ListOptions& options) {
    const std::string command = "point list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    Json points = Json::array();
    for (const Point& point : floor->points) {
        points.push_back(pointEntry(*floor, point));
    }
    Json result = Json::object();
    result["floor"] = floor->id;
    result["points"] = std::move(points);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> pointCommands() {
    CommandSpec add;
    add.words = {"point", "add"};
    add.description = "Add a wall corner point to a floor.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AddOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--x", options->x, "Position to the right, in metres.")->type_name("metres")->required();
        sub.add_option("--y", options->y, "Position down, in metres.")->type_name("metres")->required();
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        sub.add_option("--corner-radius", options->cornerRadius, "Corner rounding in metres. Default: 0.")
            ->type_name("metres");
        return [options](const RunContext& context) { return pointAdd(context, *options); };
    };

    CommandSpec move;
    move.words = {"point", "move"};
    move.description = "Move a point to a new position.";
    move.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<MoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the point.")->type_name("id")->required();
        sub.add_option("--x", options->x, "New position to the right, in metres.")->type_name("metres")->required();
        sub.add_option("--y", options->y, "New position down, in metres.")->type_name("metres")->required();
        return [options](const RunContext& context) { return pointMove(context, *options); };
    };

    CommandSpec join;
    join.words = {"point", "join"};
    join.description = "Join a point onto another point or a place on a wall; its walls then run there.";
    join.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<JoinOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the point to join and remove.")->type_name("id")->required();
        sub.add_option("--to", options->to, "Id of the point to join it to.")->type_name("id");
        sub.add_option("--wall", options->wall, "Id of the wall to join it to. The wall is split there.")->type_name("id");
        sub.add_option("--offset", options->offset, "With --wall: distance from the wall's first point, in metres.")
            ->type_name("metres");
        return [options](const RunContext& context) { return pointJoin(context, *options); };
    };

    CommandSpec set;
    set.words = {"point", "set"};
    set.description = "Change a point.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the point.")->type_name("id")->required();
        sub.add_option("--corner-radius", options->cornerRadius,
                       "Round the corner with this radius in metres. 0 makes it sharp.")
            ->type_name("metres")
            ->required();
        return [options](const RunContext& context) { return pointSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"point", "remove"};
    remove.description = "Remove a point that no wall uses.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_flag("--cascade", options->cascade, "Also remove the walls and rooms that depend on it.");
        sub.add_option("id", options->id, "Id of the point.")->type_name("id")->required();
        return [options](const RunContext& context) { return pointRemove(context, *options); };
    };

    CommandSpec list;
    list.words = {"point", "list"};
    list.description = "List the points of a floor.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return pointList(context, *options); };
    };

    return {add, move, join, set, remove, list};
}

}
