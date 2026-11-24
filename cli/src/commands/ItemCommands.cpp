#include "../CatalogCommon.h"
#include "../Common.h"
#include "../GeneratorCommon.h"

#include <plynth/core/Generators.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/Items.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/catalog/ItemAssets.h>
#include <plynth/core/commands/GeneratorCommands.h>
#include <plynth/core/commands/ItemCommands.h>

#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace plynth::cli {

namespace {

struct PlaceOptions {
    std::string project;
    std::string library;
    std::string asset;
    std::string generator;
    std::vector<std::string> params;
    std::string floor;
    std::string room;
    std::string wall;
    std::string side;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> rot;
    std::optional<double> z;
    std::optional<double> centre;
    std::optional<double> offset;
    std::optional<double> snap;
    bool strict = false;
};

struct ListOptions {
    std::string project;
    std::string library;
    std::string floor;
    std::string room;
    std::string wall;
};

struct MoveOptions {
    std::string project;
    std::string library;
    std::string id;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> offset;
    std::optional<double> z;
    std::optional<double> centre;
    std::optional<double> snap;
    bool strict = false;
};

struct RotateOptions {
    std::string project;
    std::string library;
    std::string id;
    std::optional<double> to;
    std::optional<double> by;
    bool strict = false;
};

struct SwapOptions {
    std::string project;
    std::string library;
    std::string id;
    std::string asset;
    bool strict = false;
};

struct AttachOptions {
    std::string project;
    std::string library;
    std::string id;
    std::string wall;
    double snap = 0.3;
    bool strict = false;
};

struct DetachOptions {
    std::string project;
    std::string library;
    std::string id;
};

struct ParamsOptions {
    std::string project;
    std::string library;
    std::string id;
    std::vector<std::string> params;
    bool reset = false;
    bool fit = false;
    bool strict = false;
};

struct RemoveOptions {
    std::string project;
    std::string id;
};

// A number as short text for a message.
std::string num(double value) {
    return std::format("{}", value);
}

// Refuses an option whose number is not finite. Returns false and fills failed when one is not.
bool checkFinite(const std::string& command, const std::vector<std::pair<const char*, std::optional<double>>>& values,
                 Answer& failed) {
    for (const auto& [name, value] : values) {
        if (value && !std::isfinite(*value)) {
            failed = failure(command, fail(ErrorCode::InvalidValue, std::string(name) + " must be a finite number."));
            return false;
        }
    }
    return true;
}

// Opens the library when there is one. A library named with --library that cannot be opened is an error;
// without the option a missing library only means have is false.
bool openOptionalCatalog(const std::string& command, const RunContext& context, const std::string& libraryOption,
                         OpenedCatalog& opened, bool& have, Answer& failed) {
    Answer refusal;
    have = openCatalog(command, context, libraryOption, opened, refusal);
    if (!have && !libraryOption.empty()) {
        failed = refusal;
        return false;
    }
    return true;
}

// The library an item answer reads: the asset catalog and the generators with their add-on cache. have is
// false when no library could be opened, and then asset and generator states are unknown.
struct ItemLibrary {
    OpenedCatalog opened;
    bool have = false;
    GeneratorEnv generators;

    const AssetCatalog* catalog() const { return have ? opened.catalog.get() : nullptr; }
    const GeneratorEnv* generatorEnv() const { return have ? &generators : nullptr; }
};

// Opens the library for an item command. With required false a missing library only means have is false,
// unless --library named one that cannot be opened. On error the answer is returned in failed.
bool openItemLibrary(const std::string& command, const RunContext& context, const std::string& libraryOption,
                     ItemLibrary& lib, Answer& failed, bool required = false) {
    if (required) {
        lib.have = openCatalog(command, context, libraryOption, lib.opened, failed);
        if (!lib.have) {
            return false;
        }
    } else if (!openOptionalCatalog(command, context, libraryOption, lib.opened, lib.have, failed)) {
        return false;
    }
    if (lib.have) {
        lib.generators = loadGeneratorEnv(lib.opened.paths, lib.opened.settings, std::nullopt);
    }
    return true;
}

// The item entry shared by the item commands. catalog is null when no library could be opened: the asset
// state is then unknown and the missing and size problems are not checked.
Json itemEntry(const Floor& floor, const Item& item, const ItemLibrary& lib) {
    const AssetCatalog* catalog = lib.catalog();
    const AssetLookup lookup = catalog != nullptr ? assetLookupFor(*catalog) : AssetLookup{};
    const GeneratorItemLookup generatorLookup = lib.have ? lib.generators.lookup() : GeneratorItemLookup{};
    const std::vector<ItemProblem> problems = checkItem(floor, item, lookup, generatorLookup);

    std::string state = "unknown";
    Json name = nullptr;
    if (catalog != nullptr && !isGeneratorItem(item)) {
        state = "ok";
        for (const ItemProblem& problem : problems) {
            if (problem.kind == ItemProblemKind::MissingAsset) {
                state = "missing";
            } else if (problem.kind == ItemProblemKind::SizeChanged) {
                state = "size_changed";
            }
        }
        if (const auto record = catalog->asset(item.asset)) {
            name = record->name;
        }
    }

    Json wallSide = nullptr;
    Json wallOffset = nullptr;
    if (item.wall) {
        const WallItemSpot spot = wallItemSpot(floor, item);
        if (spot.valid) {
            wallSide = std::string(wallSideName(spot.side));
            wallOffset = spot.offset;
        }
    }

    Json generator = nullptr;
    if (item.generator) {
        const GeneratorInfo* info = lib.have ? lib.generators.library.find(item.generator->name) : nullptr;
        Json use = Json::object();
        use["name"] = item.generator->name;
        use["display_name"] = info != nullptr ? info->name : item.generator->name;
        use["params"] = generatorParamsJson(info, item.generator->params);
        use["state"] = generatorItemState(lib.generatorEnv(), *item.generator);
        generator = std::move(use);
    }

    Json size = Json::object();
    size["width"] = item.size.width;
    size["depth"] = item.size.depth;
    size["height"] = item.size.height;

    Json footprint = Json::array();
    for (const Vec2& corner : itemFootprint(item).corners) {
        Json point = Json::object();
        point["x"] = corner.x;
        point["y"] = corner.y;
        footprint.push_back(std::move(point));
    }

    Json problemList = Json::array();
    for (const ItemProblem& problem : problems) {
        Json entry = Json::object();
        entry["code"] = std::string(itemProblemName(problem.kind));
        entry["message"] = problem.message;
        if (problem.other.empty()) {
            entry["other"] = nullptr;
        } else {
            entry["other"] = problem.other;
        }
        problemList.push_back(std::move(entry));
    }

    Json entry = Json::object();
    entry["id"] = item.id;
    entry["floor"] = floor.id;
    if (isGeneratorItem(item)) {
        entry["asset"] = nullptr;
        entry["asset_name"] = nullptr;
        entry["asset_state"] = nullptr;
    } else {
        entry["asset"] = item.asset;
        entry["asset_name"] = std::move(name);
        entry["asset_state"] = state;
    }
    entry["generator"] = std::move(generator);
    if (item.room) {
        entry["room"] = *item.room;
    } else {
        entry["room"] = nullptr;
    }
    entry["x"] = item.x;
    entry["y"] = item.y;
    entry["z"] = item.z;
    entry["rotation"] = item.rotation;
    if (item.wall) {
        entry["wall"] = *item.wall;
    } else {
        entry["wall"] = nullptr;
    }
    entry["wall_side"] = std::move(wallSide);
    entry["wall_offset"] = std::move(wallOffset);
    entry["centre_height"] = itemCentreHeight(item);
    entry["size"] = std::move(size);
    entry["footprint"] = std::move(footprint);
    entry["problems"] = std::move(problemList);
    return entry;
}

// The answer for a changed item: its entry, and its problems as warnings. With strict and any problem the
// answer is a refusal, so editProject saves nothing.
Answer itemAnswer(const std::string& command, const Document& document, const Id& id, const ItemLibrary& lib,
                  bool strict) {
    const Floor* floor = findFloorContaining(document.plan(), id);
    const Item* item = floor == nullptr ? nullptr : findItem(*floor, id);
    if (item == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no item " + id + "."));
    }
    const AssetLookup lookup = lib.catalog() != nullptr ? assetLookupFor(*lib.catalog()) : AssetLookup{};
    const GeneratorItemLookup generatorLookup = lib.have ? lib.generators.lookup() : GeneratorItemLookup{};
    const std::vector<ItemProblem> problems = checkItem(*floor, *item, lookup, generatorLookup);
    if (strict && !problems.empty()) {
        const std::string count = problems.size() == 1 ? "1 problem" : std::to_string(problems.size()) + " problems";
        return failure(command, fail(ErrorCode::InvalidValue, problems.front().message + " " + count + " in all."));
    }
    Json result = Json::object();
    result["item"] = itemEntry(*floor, *item, lib);
    Answer answer = success(command, std::move(result));
    for (const ItemProblem& problem : problems) {
        answer.warnings.push_back({std::string(itemProblemName(problem.kind)), problem.message});
    }
    return answer;
}

// The item with this id, or null with failed filled when there is none.
const Item* resolveItem(const std::string& command, const Plan& plan, const Id& id, Answer& failed) {
    const Floor* floor = findFloorContaining(plan, id);
    const Item* item = floor == nullptr ? nullptr : findItem(*floor, id);
    if (item == nullptr) {
        failed = failure(command, fail(ErrorCode::NotFound, "There is no item " + id + "."));
    }
    return item;
}

// The room named by an id, or by a type when exactly one room of the floor has that type. Null with failed
// filled otherwise.
const Room* resolveRoom(const std::string& command, const Floor& floor, const std::string& text, Answer& failed) {
    if (const Room* room = findRoom(floor, text)) {
        return room;
    }
    std::vector<const Room*> matches;
    for (const Room& room : floor.rooms) {
        if (room.type && *room.type == text) {
            matches.push_back(&room);
        }
    }
    if (matches.empty()) {
        failed = failure(command, fail(ErrorCode::NotFound, "There is no room or room type " + text + " on floor " +
                                                                floor.id + "."));
        return nullptr;
    }
    if (matches.size() > 1) {
        std::string list;
        for (const Room* room : matches) {
            list += (list.empty() ? "" : ", ") + room->id;
        }
        failed = failure(command, fail(ErrorCode::InvalidValue, "More than one room is a " + text + ": " + list +
                                                                    ". Name one by its id."));
        return nullptr;
    }
    return matches.front();
}

// The face of a wall a new wall item goes on. The side named by --side wins; then the side that faces the
// room; then the side nearest the wanted place; otherwise the right side.
WallSide chooseSide(const Floor& floor, const Wall& wall, const PlaceOptions& options, const Room* room,
                    double offset, const ItemSize& size) {
    if (!options.side.empty()) {
        return wallSideFromName(options.side).value_or(WallSide::Right);
    }
    if (room == nullptr && !(options.x && options.y)) {
        return WallSide::Right;
    }
    const ItemPose left = wallItemPose(floor, wall, WallSide::Left, offset, size);
    const ItemPose right = wallItemPose(floor, wall, WallSide::Right, offset, size);
    if (!left.status.ok() || !right.status.ok()) {
        return WallSide::Right;
    }
    if (room != nullptr) {
        const bool leftIn = roomAt(floor, left.position) == room->id;
        const bool rightIn = roomAt(floor, right.position) == room->id;
        if (leftIn != rightIn) {
            return leftIn ? WallSide::Left : WallSide::Right;
        }
        if (!(options.x && options.y)) {
            return WallSide::Right;
        }
    }
    const auto distance = [&](const Vec2& at) { return std::hypot(at.x - *options.x, at.y - *options.y); };
    return distance(left.position) < distance(right.position) ? WallSide::Left : WallSide::Right;
}

// Splits "id=value" at the first "=".
bool splitParam(const std::string& text, std::string& id, std::string& value) {
    const std::size_t at = text.find('=');
    if (at == std::string::npos || at == 0) {
        return false;
    }
    id = text.substr(0, at);
    value = text.substr(at + 1);
    return true;
}

// Sets each "id=value" text in a full set of values. InvalidValue naming the parameter when one does not fit.
Status applyParamTexts(const GeneratorInfo& info, std::vector<GeneratorParamValue>& values,
                       const std::vector<std::string>& texts) {
    for (const std::string& text : texts) {
        std::string id;
        std::string value;
        if (!splitParam(text, id, value)) {
            return fail(ErrorCode::InvalidValue, "--param needs the form id=value, not \"" + text + "\".");
        }
        const GeneratorParam* param = info.findParam(id);
        if (param == nullptr) {
            return fail(ErrorCode::InvalidValue, "Generator " + info.id + " has no parameter " + id + ".");
        }
        const GeneratorValueRead read = parseGeneratorValue(*param, value);
        if (!read.status.ok()) {
            return read.status;
        }
        const Status set = setGeneratorParam(info, values, id, read.value);
        if (!set.ok()) {
            return set;
        }
    }
    return {};
}

// Gives a new item the generator named by --generator with the defaults and the --param values. NotFound for
// an unknown generator, InvalidValue for one that needs an add-on that is off or a value that does not fit.
Status chooseGenerator(const GeneratorEnv& env, const std::string& id, const std::vector<std::string>& texts,
                       double ceilingHeight, Item& item) {
    const GeneratorInfo* info = env.library.find(id);
    if (info == nullptr) {
        return fail(ErrorCode::NotFound, "There is no generator " + id + " in the library.");
    }
    const GeneratorState state = generatorState(*info, env.cache);
    if (state.availability == GeneratorAvailability::NeedsAddon) {
        return fail(ErrorCode::InvalidValue, state.message);
    }
    std::vector<GeneratorParamValue> values = defaultGeneratorParams(*info, ceilingHeight);
    const Status applied = applyParamTexts(*info, values, texts);
    if (!applied.ok()) {
        return applied;
    }
    const GeneratorPlacement placement = makeGeneratorUse(*info, values);
    if (!placement.status.ok()) {
        return placement.status;
    }
    item.generator = placement.use;
    item.size = placement.size;
    return {};
}

// Places an asset or a generator on the floor or on a wall.
Answer itemPlace(const RunContext& context, const PlaceOptions& options) {
    const std::string command = "place";
    const bool byXY = options.x.has_value() || options.y.has_value();
    if (options.asset.empty() == options.generator.empty()) {
        return badArguments(command, "bad_arguments", "Give an asset id or --generator, one of them.");
    }
    if (!options.params.empty() && options.generator.empty()) {
        return badArguments(command, "bad_arguments", "--param is for --generator.");
    }
    if (byXY && !(options.x && options.y)) {
        return badArguments(command, "bad_arguments", "Give both --x and --y.");
    }
    if (!byXY && options.room.empty() && options.wall.empty()) {
        return badArguments(command, "bad_arguments", "Give a place: --x and --y, --room, or --wall.");
    }
    if (options.rot && !options.wall.empty()) {
        return badArguments(command, "bad_arguments", "--rot cannot be used with --wall. A wall item faces away from its wall.");
    }
    if (options.z && options.centre) {
        return badArguments(command, "bad_arguments", "Give --z or --centre, not both.");
    }
    if (options.wall.empty() && (options.offset || !options.side.empty())) {
        return badArguments(command, "bad_arguments", "--offset and --side are for --wall.");
    }
    if (!options.wall.empty() && options.snap) {
        return badArguments(command, "bad_arguments", "--snap is for items on the floor.");
    }
    Answer invalid;
    if (!checkFinite(command, {{"--x", options.x}, {"--y", options.y}, {"--rot", options.rot}, {"--z", options.z},
                               {"--centre", options.centre}, {"--offset", options.offset}, {"--snap", options.snap}},
                     invalid)) {
        return invalid;
    }
    if (options.snap && *options.snap < 0.0) {
        return failure(command, fail(ErrorCode::InvalidValue, "--snap cannot be negative."));
    }

    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
        if (floor == nullptr) {
            return failed;
        }
        ItemLibrary lib;
        if (!openItemLibrary(command, context, options.library, lib, failed, true)) {
            return failed;
        }
        Item item;
        if (options.generator.empty()) {
            const ItemAsset asset = itemAssetFor(*lib.opened.catalog, options.asset);
            if (!asset.status.ok()) {
                return failure(command, asset.status);
            }
            item.asset = options.asset;
            item.size = asset.size;
        } else {
            const Status chosen = chooseGenerator(lib.generators, options.generator, options.params,
                                                  floor->ceilingHeight, item);
            if (!chosen.ok()) {
                return failure(command, chosen);
            }
        }

        const Room* room = nullptr;
        if (!options.room.empty()) {
            room = resolveRoom(command, *floor, options.room, failed);
            if (room == nullptr) {
                return failed;
            }
            if (byXY && roomAt(*floor, {*options.x, *options.y}) != room->id) {
                return failure(command, fail(ErrorCode::InvalidValue, "The place (" + num(*options.x) + ", " +
                                                                          num(*options.y) + ") is not inside room " +
                                                                          room->id + "."));
            }
        }

        std::optional<double> z = options.z;
        if (options.centre) {
            z = *options.centre - item.size.height / 2.0;
        }

        if (!options.wall.empty()) {
            const Wall* wall = findWall(*floor, options.wall);
            if (wall == nullptr) {
                return failure(command, fail(ErrorCode::NotFound, "There is no wall " + options.wall + " on floor " +
                                                                      floor->id + "."));
            }
            const double offset = options.offset.value_or(wallLength(*floor, *wall) / 2.0);
            const WallSide side = chooseSide(*floor, *wall, options, room, offset, item.size);
            const ItemPose pose = wallItemPose(*floor, *wall, side, offset, item.size);
            if (!pose.status.ok()) {
                return failure(command, pose.status);
            }
            item.x = pose.position.x;
            item.y = pose.position.y;
            item.rotation = pose.rotation;
            item.wall = wall->id;
            item.z = z.value_or(defaultWallItemZ(document.project().settings.defaults, item.size.height));
        } else {
            Vec2 position;
            if (byXY) {
                position = {*options.x, *options.y};
            } else {
                const RoomShape shape = roomShape(*floor, *room);
                if (!shape.closed) {
                    return failure(command, fail(ErrorCode::InvalidValue, "Room " + room->id + " is not closed."));
                }
                position = shape.labelPoint;
            }
            item.rotation = normalizeRotation(options.rot.value_or(0.0));
            item.z = z.value_or(0.0);
            if (options.snap && *options.snap > 0.0) {
                ItemSnapOptions snap;
                snap.gridSize = document.project().settings.gridSize;
                snap.radius = *options.snap;
                position = snapItemPosition(*floor, item, position, snap).position;
            }
            item.x = position.x;
            item.y = position.y;
        }

        auto placed = std::make_unique<PlaceItemCommand>(floor->id, item);
        const PlaceItemCommand* raw = placed.get();
        if (!runCommand(command, document, std::move(placed), failed)) {
            return failed;
        }
        return itemAnswer(command, document, raw->itemId(), lib, options.strict);
    });
}

// Lists the items of a floor, optionally those in one room or on one wall.
Answer itemList(const RunContext& context, const ListOptions& options) {
    const std::string command = "item list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    if (!options.room.empty() && findRoom(*floor, options.room) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no room " + options.room + " on floor " +
                                                              floor->id + "."));
    }
    if (!options.wall.empty() && findWall(*floor, options.wall) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no wall " + options.wall + " on floor " +
                                                              floor->id + "."));
    }
    ItemLibrary lib;
    if (!openItemLibrary(command, context, options.library, lib, failed)) {
        return failed;
    }
    Json items = Json::array();
    for (const Item& item : floor->items) {
        if (!options.room.empty() && item.room != options.room) {
            continue;
        }
        if (!options.wall.empty() && item.wall != options.wall) {
            continue;
        }
        items.push_back(itemEntry(*floor, item, lib));
    }
    Json result = Json::object();
    const std::size_t count = items.size();
    result["items"] = std::move(items);
    result["count"] = count;
    return success(command, std::move(result));
}

// Moves an item on the plan, along its wall, or up and down. What is left out stays.
Answer itemMove(const RunContext& context, const MoveOptions& options) {
    const std::string command = "item move";
    const bool byXY = options.x.has_value() || options.y.has_value();
    if (byXY && !(options.x && options.y)) {
        return badArguments(command, "bad_arguments", "Give both --x and --y.");
    }
    if (!byXY && !options.offset && !options.z && !options.centre) {
        return badArguments(command, "bad_arguments", "Give at least one of --x and --y, --offset, --z and --centre.");
    }
    if (byXY && options.offset) {
        return badArguments(command, "bad_arguments", "Give --x and --y, or --offset, not both.");
    }
    if (options.z && options.centre) {
        return badArguments(command, "bad_arguments", "Give --z or --centre, not both.");
    }
    if (options.snap && !byXY) {
        return badArguments(command, "bad_arguments", "--snap needs --x and --y.");
    }
    Answer invalid;
    if (!checkFinite(command, {{"--x", options.x}, {"--y", options.y}, {"--offset", options.offset},
                               {"--z", options.z}, {"--centre", options.centre}, {"--snap", options.snap}},
                     invalid)) {
        return invalid;
    }
    if (options.snap && *options.snap < 0.0) {
        return failure(command, fail(ErrorCode::InvalidValue, "--snap cannot be negative."));
    }

    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Item* item = resolveItem(command, document.plan(), options.id, failed);
        if (item == nullptr) {
            return failed;
        }
        const Floor* floor = findFloorContaining(document.plan(), options.id);
        if (options.offset && !item->wall) {
            return badArguments(command, "bad_arguments",
                                "--offset is for wall items. " + options.id + " stands on the floor.");
        }
        if (options.snap && item->wall) {
            return badArguments(command, "bad_arguments", "--snap is for items on the floor.");
        }
        std::optional<double> z = options.z;
        if (options.centre) {
            z = *options.centre - item->size.height / 2.0;
        }

        auto group = std::make_unique<CommandGroup>("Move item");
        if (options.offset) {
            group->add(std::make_unique<MoveItemOnWallCommand>(options.id, *options.offset, z.value_or(item->z)));
        } else {
            if (byXY) {
                Vec2 position = {*options.x, *options.y};
                if (options.snap && *options.snap > 0.0) {
                    ItemSnapOptions snap;
                    snap.gridSize = document.project().settings.gridSize;
                    snap.radius = *options.snap;
                    position = snapItemPosition(*floor, *item, position, snap).position;
                }
                group->add(std::make_unique<MoveItemCommand>(options.id, position));
            }
            if (z) {
                group->add(std::make_unique<SetItemHeightCommand>(options.id, *z));
            }
        }
        ItemLibrary lib;
        if (!openItemLibrary(command, context, options.library, lib, failed)) {
            return failed;
        }
        if (!runCommand(command, document, std::move(group), failed)) {
            return failed;
        }
        return itemAnswer(command, document, options.id, lib, options.strict);
    });
}

// Turns a floor item to a rotation or by an angle.
Answer itemRotate(const RunContext& context, const RotateOptions& options) {
    const std::string command = "item rotate";
    if (options.to.has_value() == options.by.has_value()) {
        return badArguments(command, "bad_arguments", "Give --to or --by, one of them.");
    }
    Answer invalid;
    if (!checkFinite(command, {{"--to", options.to}, {"--by", options.by}}, invalid)) {
        return invalid;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Item* item = resolveItem(command, document.plan(), options.id, failed);
        if (item == nullptr) {
            return failed;
        }
        const double rotation = options.to ? *options.to : item->rotation + *options.by;
        ItemLibrary lib;
        if (!openItemLibrary(command, context, options.library, lib, failed)) {
            return failed;
        }
        if (!runCommand(command, document, std::make_unique<RotateItemCommand>(options.id, rotation), failed)) {
            return failed;
        }
        return itemAnswer(command, document, options.id, lib, options.strict);
    });
}

// Puts another asset in an item's place, or the same asset with its size from the library.
Answer itemSwap(const RunContext& context, const SwapOptions& options) {
    const std::string command = "item swap";
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        if (resolveItem(command, document.plan(), options.id, failed) == nullptr) {
            return failed;
        }
        ItemLibrary lib;
        if (!openItemLibrary(command, context, options.library, lib, failed, true)) {
            return failed;
        }
        const ItemAsset asset = itemAssetFor(*lib.opened.catalog, options.asset);
        if (!asset.status.ok()) {
            return failure(command, asset.status);
        }
        if (!runCommand(command, document,
                        std::make_unique<SwapItemAssetCommand>(options.id, options.asset, asset.size), failed)) {
            return failed;
        }
        return itemAnswer(command, document, options.id, lib, options.strict);
    });
}

// Mounts an item on a wall: the one named by --wall, or the nearest wall within --snap of the item.
Answer itemAttach(const RunContext& context, const AttachOptions& options) {
    const std::string command = "item attach";
    if (!std::isfinite(options.snap) || options.snap < 0.0) {
        return failure(command, fail(ErrorCode::InvalidValue, "--snap must be zero or more."));
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Item* item = resolveItem(command, document.plan(), options.id, failed);
        if (item == nullptr) {
            return failed;
        }
        Id wall = options.wall;
        if (wall.empty()) {
            const Floor* floor = findFloorContaining(document.plan(), options.id);
            wall = nearestWallForMount(*floor, {item->x, item->y}, options.snap);
            if (wall.empty()) {
                return failure(command, fail(ErrorCode::NotFound, "There is no wall within " + num(options.snap) +
                                                                      " m of " + options.id + ". Give --wall."));
            }
        }
        ItemLibrary lib;
        if (!openItemLibrary(command, context, options.library, lib, failed)) {
            return failed;
        }
        if (!runCommand(command, document, std::make_unique<SetItemWallCommand>(options.id, wall), failed)) {
            return failed;
        }
        return itemAnswer(command, document, options.id, lib, options.strict);
    });
}

// Takes an item off its wall. It keeps its place, rotation and height.
Answer itemDetach(const RunContext& context, const DetachOptions& options) {
    const std::string command = "item detach";
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        if (resolveItem(command, document.plan(), options.id, failed) == nullptr) {
            return failed;
        }
        ItemLibrary lib;
        if (!openItemLibrary(command, context, options.library, lib, failed)) {
            return failed;
        }
        if (!runCommand(command, document, std::make_unique<SetItemWallCommand>(options.id, std::nullopt), failed)) {
            return failed;
        }
        return itemAnswer(command, document, options.id, lib, false);
    });
}

// Changes the values of a generator item, puts its defaults back, or fits them to the generator as it is now.
Answer itemParams(const RunContext& context, const ParamsOptions& options) {
    const std::string command = "item params";
    if (options.reset && options.fit) {
        return badArguments(command, "bad_arguments", "Give --reset or --fit, not both.");
    }
    if (options.params.empty() && !options.reset && !options.fit) {
        return badArguments(command, "bad_arguments", "Give at least one of --param, --reset and --fit.");
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Item* item = resolveItem(command, document.plan(), options.id, failed);
        if (item == nullptr) {
            return failed;
        }
        if (!item->generator) {
            return failure(command, fail(ErrorCode::InvalidValue, "Item " + options.id +
                                                                      " is an asset, not a generator item."));
        }
        const Floor* floor = findFloorContaining(document.plan(), options.id);
        ItemLibrary lib;
        if (!openItemLibrary(command, context, options.library, lib, failed, true)) {
            return failed;
        }
        const GeneratorInfo* info = lib.generators.library.find(item->generator->name);
        if (info == nullptr) {
            return failure(command, fail(ErrorCode::NotFound, "There is no generator " + item->generator->name +
                                                                  " in the library."));
        }
        std::vector<GeneratorParamValue> values = item->generator->params;
        if (options.reset) {
            values = defaultGeneratorParams(*info, floor->ceilingHeight);
        } else if (options.fit) {
            values = fitGeneratorParams(*info, values, floor->ceilingHeight).params;
        } else if (!checkGeneratorParams(*info, values).ok()) {
            return failure(command, fail(ErrorCode::InvalidValue,
                                         "The values of " + options.id + " no longer fit generator " + info->id +
                                             ". Use --fit to update them or --reset for the defaults."));
        }
        const Status applied = applyParamTexts(*info, values, options.params);
        if (!applied.ok()) {
            return failure(command, applied);
        }
        const GeneratorPlacement placement = makeGeneratorUse(*info, values);
        if (!placement.status.ok()) {
            return failure(command, placement.status);
        }
        if (!runCommand(command, document,
                        std::make_unique<SetItemGeneratorCommand>(options.id, placement.use, placement.size),
                        failed)) {
            return failed;
        }
        return itemAnswer(command, document, options.id, lib, options.strict);
    });
}

// Removes an item.
Answer itemRemove(const RunContext& context, const RemoveOptions& options) {
    return removeCascade("item remove", "item", context, options.project, options.id);
}

// Adds the --strict flag.
void addStrictOption(CLI::App& sub, bool& value) {
    sub.add_flag("--strict", value, "Refuse when the item would have any problem (overlap, outside its room...).");
}

}

std::vector<CommandSpec> itemCommands() {
    CommandSpec place;
    place.words = {"place"};
    place.description = "Place a library asset or a generator on the floor or on a wall.";
    place.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<PlaceOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("asset", options->asset, "Id of the asset in the library. Not with --generator.")
            ->type_name("id");
        sub.add_option("--generator", options->generator, "Id of a generator to place instead of an asset.")
            ->type_name("id");
        sub.add_option("--param", options->params,
                       "With --generator: a parameter value as id=value. Repeat for more. The rest keep their defaults.")
            ->type_name("id=value");
        sub.add_option("--x", options->x, "Centre on the plan, to the right, in metres. Use with --y.")
            ->type_name("metres");
        sub.add_option("--y", options->y, "Centre on the plan, down, in metres. Use with --x.")->type_name("metres");
        sub.add_option("--room", options->room,
                       "A room id, or a room type when one room has it. Alone it places at the room's label point.")
            ->type_name("id|type");
        sub.add_option("--wall", options->wall, "Id of the wall to mount the item on.")->type_name("id");
        sub.add_option("--rot", options->rot, "Rotation in degrees, clockwise. Default: 0. Not with --wall.")
            ->type_name("degrees");
        sub.add_option("--z", options->z, "Height of the bottom above the floor, in metres.")->type_name("metres");
        sub.add_option("--centre", options->centre, "Height of the centre above the floor, in metres.")
            ->type_name("metres");
        sub.add_option("--offset", options->offset,
                       "With --wall: distance along the wall to the centre. Default: the middle.")
            ->type_name("metres");
        sub.add_option("--side", options->side, "With --wall: the face to hang on. Default: the side facing --room.")
            ->type_name("left|right")
            ->check(CLI::IsMember({"left", "right"}));
        sub.add_option("--snap", options->snap,
                       "Snap to the grid and to wall faces within this distance, in metres. Floor items only.")
            ->type_name("metres");
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        addStrictOption(sub, options->strict);
        return [options](const RunContext& context) { return itemPlace(context, *options); };
    };

    CommandSpec list;
    list.words = {"item", "list"};
    list.description = "List the items of a floor with their problems.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        sub.add_option("--room", options->room, "Only the items in this room.")->type_name("id");
        sub.add_option("--wall", options->wall, "Only the items mounted on this wall.")->type_name("id");
        return [options](const RunContext& context) { return itemList(context, *options); };
    };

    CommandSpec move;
    move.words = {"item", "move"};
    move.description = "Move an item on the plan, along its wall, or up and down.";
    move.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<MoveOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the item.")->type_name("id")->required();
        sub.add_option("--x", options->x, "New centre on the plan, to the right, in metres. Use with --y.")
            ->type_name("metres");
        sub.add_option("--y", options->y, "New centre on the plan, down, in metres. Use with --x.")
            ->type_name("metres");
        sub.add_option("--offset", options->offset, "Wall items: new distance along the wall to the centre.")
            ->type_name("metres");
        sub.add_option("--z", options->z, "New height of the bottom above the floor, in metres.")
            ->type_name("metres");
        sub.add_option("--centre", options->centre, "New height of the centre above the floor, in metres.")
            ->type_name("metres");
        sub.add_option("--snap", options->snap,
                       "With --x and --y: snap to the grid and to wall faces within this distance, in metres.")
            ->type_name("metres");
        addStrictOption(sub, options->strict);
        return [options](const RunContext& context) { return itemMove(context, *options); };
    };

    CommandSpec rotate;
    rotate.words = {"item", "rotate"};
    rotate.description = "Turn a floor item.";
    rotate.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RotateOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the item.")->type_name("id")->required();
        sub.add_option("--to", options->to, "The new rotation in degrees, clockwise.")->type_name("degrees");
        sub.add_option("--by", options->by, "Turn by this many degrees, clockwise.")->type_name("degrees");
        addStrictOption(sub, options->strict);
        return [options](const RunContext& context) { return itemRotate(context, *options); };
    };

    CommandSpec swap;
    swap.words = {"item", "swap"};
    swap.description = "Put another asset in an item's place, or refresh its size from the library.";
    swap.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SwapOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the item.")->type_name("id")->required();
        sub.add_option("asset", options->asset, "Id of the asset in the library.")->type_name("id")->required();
        addStrictOption(sub, options->strict);
        return [options](const RunContext& context) { return itemSwap(context, *options); };
    };

    CommandSpec params;
    params.words = {"item", "params"};
    params.description = "Change the values of a generator item, reset them or fit them to the generator.";
    params.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ParamsOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the item.")->type_name("id")->required();
        sub.add_option("--param", options->params, "A parameter value as id=value. Repeat for more.")
            ->type_name("id=value");
        sub.add_flag("--reset", options->reset, "Put every value back to its default.");
        sub.add_flag("--fit", options->fit, "Keep the values that still fit the generator and default the rest.");
        addStrictOption(sub, options->strict);
        return [options](const RunContext& context) { return itemParams(context, *options); };
    };

    CommandSpec attach;
    attach.words = {"item", "attach"};
    attach.description = "Mount an item on a wall.";
    attach.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AttachOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the item.")->type_name("id")->required();
        sub.add_option("--wall", options->wall, "Id of the wall. Default: the nearest wall within --snap.")
            ->type_name("id");
        sub.add_option("--snap", options->snap, "Without --wall: how far a wall may be from the item, in metres. Default: 0.3.")
            ->type_name("metres");
        addStrictOption(sub, options->strict);
        return [options](const RunContext& context) { return itemAttach(context, *options); };
    };

    CommandSpec detach;
    detach.words = {"item", "detach"};
    detach.description = "Take an item off its wall. It keeps its place and height.";
    detach.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<DetachOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the item.")->type_name("id")->required();
        return [options](const RunContext& context) { return itemDetach(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"item", "remove"};
    remove.description = "Remove an item.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the item.")->type_name("id")->required();
        return [options](const RunContext& context) { return itemRemove(context, *options); };
    };

    return {place, list, move, rotate, swap, params, attach, detach, remove};
}

}
