#include "../Common.h"

#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/commands/RoomCommands.h>

#include <cmath>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct AddOptions {
    std::string project;
    std::string floor;
    double x = 0.0;
    double y = 0.0;
    std::optional<std::string> type;
};

struct SetOptions {
    std::string project;
    std::string id;
    std::string type;
};

struct RemoveOptions {
    std::string project;
    std::string id;
};

struct ListOptions {
    std::string project;
    std::string floor;
};

// Rounds an area to four decimals, which hides the small error of the polygon arithmetic in the answer.
double roundedArea(double area) {
    return std::round(area * 1e4) / 1e4;
}

// The room entry for an id, read from the changed plan.
Json entryFor(const Plan& plan, const Id& id) {
    const Floor* floor = findFloorContaining(plan, id);
    return roomEntry(*floor, *findRoom(*floor, id));
}

// Adds a room for the closed area around a position.
Answer roomAdd(const RunContext& context, const AddOptions& options) {
    const std::string name = "room add";
    return editProject(name, context, options.project, [&](Document& document) {
        if (options.type) {
            const Status known = checkRoomType(*options.type);
            if (!known.ok()) {
                return failure(name, known);
            }
        }
        Answer failed;
        const Floor* floor = resolveFloor(name, document.plan(), options.floor, failed);
        if (floor == nullptr) {
            return failed;
        }
        auto command = std::make_unique<AddRoomCommand>(floor->id, Vec2{options.x, options.y}, options.type);
        const AddRoomCommand* raw = command.get();
        if (!runCommand(name, document, std::move(command), failed)) {
            return failed;
        }
        return success(name, entryFor(document.plan(), raw->roomId()));
    });
}

// Sets or clears the type of a room.
Answer roomSet(const RunContext& context, const SetOptions& options) {
    const std::string name = "room set";
    return editProject(name, context, options.project, [&](Document& document) {
        std::optional<std::string> type;
        if (options.type != "none") {
            const Status known = checkRoomType(options.type);
            if (!known.ok()) {
                return failure(name, known);
            }
            type = options.type;
        }
        Answer failed;
        if (!runCommand(name, document, std::make_unique<SetRoomTypeCommand>(options.id, type), failed)) {
            return failed;
        }
        return success(name, entryFor(document.plan(), options.id));
    });
}

// Removes a room. Its walls stay.
Answer roomRemove(const RunContext& context, const RemoveOptions& options) {
    return edit(
        "room remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveRoomCommand>(options.id); },
        [&](const Plan&, const RemoveRoomCommand&) { return removedJson({}, {options.id}, {}, {}); });
}

// Lists the rooms of a floor.
Answer roomList(const RunContext& context, const ListOptions& options) {
    const std::string name = "room list";
    Document document;
    Answer failed;
    if (!openProject(name, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(name, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    Json rooms = Json::array();
    for (const Room& room : floor->rooms) {
        rooms.push_back(roomEntry(*floor, room));
    }
    Json result = Json::object();
    result["floor"] = floor->id;
    result["rooms"] = std::move(rooms);
    return success(name, std::move(result));
}

// Lists the closed areas the walls of a floor make, and which of them already hold a room.
Answer roomFind(const RunContext& context, const ListOptions& options) {
    const std::string name = "room find";
    Document document;
    Answer failed;
    if (!openProject(name, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(name, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    Json areas = Json::array();
    for (const ClosedArea& area : findClosedAreas(*floor)) {
        Json inside = Json::object();
        inside["x"] = area.inside.x;
        inside["y"] = area.inside.y;
        const Id room = roomAt(*floor, area.inside);
        Json item = Json::object();
        item["walls"] = area.walls;
        item["centre_line_area"] = roundedArea(area.centreLineArea);
        item["inside"] = std::move(inside);
        if (room.empty()) {
            item["room"] = nullptr;
        } else {
            item["room"] = room;
        }
        areas.push_back(std::move(item));
    }
    Json result = Json::object();
    result["floor"] = floor->id;
    result["areas"] = std::move(areas);
    return success(name, std::move(result));
}

// Lists the built-in room types. Needs no project.
Answer roomTypes() {
    Json types = Json::array();
    for (const RoomType& type : builtInRoomTypes()) {
        Json item = Json::object();
        item["id"] = std::string(type.id);
        item["name"] = std::string(type.name);
        types.push_back(std::move(item));
    }
    Json result = Json::object();
    result["room_types"] = std::move(types);
    return success("room types", std::move(result));
}

}

Json roomEntry(const Floor& floor, const Room& room) {
    const RoomShape shape = roomShape(floor, room);
    Json label = Json::object();
    label["x"] = shape.labelPoint.x;
    label["y"] = shape.labelPoint.y;
    Json entry = Json::object();
    entry["id"] = room.id;
    entry["floor"] = floor.id;
    if (room.type) {
        entry["type"] = *room.type;
    } else {
        entry["type"] = nullptr;
    }
    entry["type_name"] = roomTypeName(room.type);
    entry["walls"] = room.walls;
    entry["closed"] = shape.closed;
    entry["area"] = roundedArea(shape.area);
    entry["label"] = std::move(label);
    return entry;
}

std::vector<CommandSpec> roomCommands() {
    CommandSpec add;
    add.words = {"room", "add"};
    add.description = "Make a room from the closed area that holds a position.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AddOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--x", options->x, "A position inside the area, to the right, in metres.")
            ->type_name("metres")
            ->required();
        sub.add_option("--y", options->y, "A position inside the area, down, in metres.")
            ->type_name("metres")
            ->required();
        sub.add_option("--type", options->type, "Room type id. Run 'room types' to list them.")->type_name("id");
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return roomAdd(context, *options); };
    };

    CommandSpec set;
    set.words = {"room", "set"};
    set.description = "Change the type of a room.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the room.")->type_name("id")->required();
        sub.add_option("--type", options->type, "Room type id, or none to clear it.")->type_name("id|none")->required();
        return [options](const RunContext& context) { return roomSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"room", "remove"};
    remove.description = "Remove a room. Its walls stay.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the room.")->type_name("id")->required();
        return [options](const RunContext& context) { return roomRemove(context, *options); };
    };

    CommandSpec list;
    list.words = {"room", "list"};
    list.description = "List the rooms of a floor with their type and area.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return roomList(context, *options); };
    };

    CommandSpec find;
    find.words = {"room", "find"};
    find.description = "List the closed areas the walls make and which of them hold a room.";
    find.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        return [options](const RunContext& context) { return roomFind(context, *options); };
    };

    CommandSpec types;
    types.words = {"room", "types"};
    types.description = "List the room types.";
    types.setup = [](CLI::App&) -> Handler {
        return [](const RunContext&) { return roomTypes(); };
    };

    return {add, set, remove, list, find, types};
}

}
