#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/OpeningAssets.h>
#include <plynth/core/OpeningShapes.h>
#include <plynth/core/Openings.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/FinishCommands.h>
#include <plynth/core/commands/OpeningCommands.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct AddOptions {
    std::string project;
    std::string floor;
    std::string wall;
    std::optional<double> offset;
    std::optional<double> x;
    std::optional<double> y;
    double snap = 0.3;
    double step = 0.0;
    std::string type = "door";
    std::string shape = "rectangle";
    std::optional<double> width;
    std::optional<double> height;
    std::optional<double> sill;
    std::string hinge;
    std::string swing;
};

struct MoveOptions {
    std::string project;
    std::string id;
    std::string wall;
    std::optional<double> offset;
    std::optional<double> sill;
};

struct ResizeOptions {
    std::string project;
    std::string id;
    std::optional<double> width;
    std::optional<double> height;
    std::optional<double> offset;
    std::optional<double> sill;
};

struct SetOptions {
    std::string project;
    std::string library;
    std::string id;
    std::string type;
    std::string shape;
    std::string hinge;
    std::string swing;
    std::string frame;
};

struct RemoveOptions {
    std::string project;
    std::string id;
};

struct ListOptions {
    std::string project;
    std::string floor;
    std::string wall;
};

// The opening entry for an id, read from the changed plan.
Json entryFor(const Plan& plan, const Id& id) {
    const Floor* floor = findFloorContaining(plan, id);
    return openingEntry(*floor, *findOpening(*floor, id));
}

// The opening with this id, or null with failed filled when there is none.
const Opening* resolveOpening(const std::string& command, const Plan& plan, const Id& id, Answer& failed) {
    const Floor* floor = findFloorContaining(plan, id);
    const Opening* opening = floor == nullptr ? nullptr : findOpening(*floor, id);
    if (opening == nullptr) {
        failed = failure(command, fail(ErrorCode::NotFound, "There is no opening " + id + "."));
    }
    return opening;
}

// Adds an opening, either on a named wall at an offset or at a place on the plan.
Answer openingAdd(const RunContext& context, const AddOptions& options) {
    const std::string command = "opening add";
    const bool byWall = !options.wall.empty() || options.offset.has_value();
    const bool byPlace = options.x.has_value() || options.y.has_value();
    if (byWall == byPlace) {
        return badArguments(command, "bad_arguments",
                            "Give either --wall and --offset, or --x and --y, but not both.");
    }
    if (byWall && (options.wall.empty() || !options.offset)) {
        return badArguments(command, "bad_arguments", "Give both --wall and --offset.");
    }
    if (byPlace && !(options.x && options.y)) {
        return badArguments(command, "bad_arguments", "Give both --x and --y.");
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const OpeningType type = openingTypeFromName(options.type).value_or(OpeningType::Door);
        Opening opening = defaultOpening(document.project().settings.defaults, type);
        opening.shape = options.shape;
        opening.width = options.width.value_or(opening.width);
        opening.height = options.height.value_or(opening.height);
        opening.sill = options.sill.value_or(opening.sill);
        if (!options.hinge.empty()) {
            opening.hinge = hingeEndFromName(options.hinge).value_or(opening.hinge);
        }
        if (!options.swing.empty()) {
            opening.swing = wallSideFromName(options.swing).value_or(opening.swing);
        }
        if (byWall) {
            opening.wall = options.wall;
            opening.offset = *options.offset;
        } else {
            const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
            if (floor == nullptr) {
                return failed;
            }
            const OpeningSpot spot =
                placeOpeningAt(*floor, {*options.x, *options.y}, options.snap, opening.width, options.step);
            if (!spot.status.ok()) {
                return failure(command, spot.status);
            }
            opening.wall = spot.wall;
            opening.offset = spot.offset;
        }
        auto added = std::make_unique<AddOpeningCommand>(opening);
        const AddOpeningCommand* raw = added.get();
        if (!runCommand(command, document, std::move(added), failed)) {
            return failed;
        }
        return success(command, entryFor(document.plan(), raw->openingId()));
    });
}

// Moves an opening to another place on its wall or onto another wall. What is left out stays.
Answer openingMove(const RunContext& context, const MoveOptions& options) {
    const std::string command = "opening move";
    if (options.wall.empty() && !options.offset && !options.sill) {
        return badArguments(command, "bad_arguments", "Give at least one of --wall, --offset and --sill.");
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Opening* current = resolveOpening(command, document.plan(), options.id, failed);
        if (current == nullptr) {
            return failed;
        }
        const Id wall = options.wall.empty() ? current->wall : options.wall;
        const double offset = options.offset.value_or(current->offset);
        const double sill = options.sill.value_or(current->sill);
        if (!runCommand(command, document, std::make_unique<MoveOpeningCommand>(options.id, wall, offset, sill),
                        failed)) {
            return failed;
        }
        return success(command, entryFor(document.plan(), options.id));
    });
}

// Gives an opening a new size. What is left out stays.
Answer openingResize(const RunContext& context, const ResizeOptions& options) {
    const std::string command = "opening resize";
    if (!options.width && !options.height) {
        return badArguments(command, "bad_arguments", "Give at least one of --width and --height.");
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Opening* current = resolveOpening(command, document.plan(), options.id, failed);
        if (current == nullptr) {
            return failed;
        }
        auto resize = std::make_unique<ResizeOpeningCommand>(
            options.id, options.offset.value_or(current->offset), options.width.value_or(current->width),
            options.sill.value_or(current->sill), options.height.value_or(current->height));
        if (!runCommand(command, document, std::move(resize), failed)) {
            return failed;
        }
        return success(command, entryFor(document.plan(), options.id));
    });
}

// Checks that an asset named by --frame is in the library and in the category the opening type takes. Fills
// failed when it is not.
bool checkFrameAsset(const std::string& command, const RunContext& context, const SetOptions& options,
                     OpeningType type, Answer& failed) {
    if (type == OpeningType::Plain) {
        failed = failure(command, fail(ErrorCode::InvalidValue,
                                       "A plain opening takes no door or window asset. Use --frame auto or generated."));
        return false;
    }
    OpenedCatalog opened;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return false;
    }
    const std::optional<AssetRecord> record = opened.catalog->asset(options.frame);
    if (!record) {
        failed = failure(command, fail(ErrorCode::NotFound, "There is no asset " + options.frame + " in the library."));
        return false;
    }
    const std::string category(type == OpeningType::Door ? kDoorCategory : kWindowCategory);
    if (std::ranges::find(record->categories, category) == record->categories.end()) {
        failed = failure(command, fail(ErrorCode::InvalidValue, "The asset " + options.frame + " is not in the " +
                                                                    category + " category, so it cannot fill a " +
                                                                    category + "."));
        return false;
    }
    return true;
}

// Changes the type, shape, hinge, swing or door or window asset of an opening, as one step. What is left out stays.
Answer openingSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "opening set";
    const bool styled = !options.type.empty() || !options.shape.empty() || !options.hinge.empty() || !options.swing.empty();
    if (!styled && options.frame.empty()) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --type, --shape, --hinge, --swing and --frame.");
    }
    const bool namedAsset = !options.frame.empty() && options.frame != "auto" && options.frame != "generated";
    if (namedAsset) {
        Answer failed;
        if (!checkIdArgument(command, options.frame, failed)) {
            return failed;
        }
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Opening* current = resolveOpening(command, document.plan(), options.id, failed);
        if (current == nullptr) {
            return failed;
        }
        OpeningStyle style{current->type, current->shape, current->hinge, current->swing};
        if (!options.type.empty()) {
            style.type = openingTypeFromName(options.type).value_or(style.type);
        }
        if (!options.shape.empty()) {
            style.shape = options.shape;
        }
        if (!options.hinge.empty()) {
            style.hinge = hingeEndFromName(options.hinge).value_or(style.hinge);
        }
        if (!options.swing.empty()) {
            style.swing = wallSideFromName(options.swing).value_or(style.swing);
        }
        auto group = std::make_unique<CommandGroup>("Change opening");
        if (styled) {
            group->add(std::make_unique<SetOpeningStyleCommand>(options.id, style));
        }
        if (namedAsset) {
            if (!checkFrameAsset(command, context, options, style.type, failed)) {
                return failed;
            }
            group->add(std::make_unique<SetOpeningFrameCommand>(options.id, options.frame, false));
        } else if (!options.frame.empty()) {
            group->add(std::make_unique<SetOpeningFrameCommand>(options.id, std::nullopt, options.frame == "auto"));
        }
        if (!runCommand(command, document, std::move(group), failed)) {
            return failed;
        }
        return success(command, entryFor(document.plan(), options.id));
    });
}

// Removes an opening.
Answer openingRemove(const RunContext& context, const RemoveOptions& options) {
    return removeCascade("opening remove", "opening", context, options.project, options.id);
}

// Lists the openings of a floor, optionally those on one wall, and how many of them do not fit.
Answer openingList(const RunContext& context, const ListOptions& options) {
    const std::string command = "opening list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
    if (floor == nullptr) {
        return failed;
    }
    if (!options.wall.empty() && findWall(*floor, options.wall) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no wall " + options.wall + " on floor " +
                                                              floor->id + "."));
    }
    Json openings = Json::array();
    int problems = 0;
    for (const Opening& opening : floor->openings) {
        if (!options.wall.empty() && opening.wall != options.wall) {
            continue;
        }
        Json entry = openingEntry(*floor, opening);
        if (!entry.at("fits").get<bool>()) {
            ++problems;
        }
        openings.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["floor"] = floor->id;
    result["openings"] = std::move(openings);
    result["problems"] = problems;
    return success(command, std::move(result));
}

// Lists the opening shapes that are registered. Needs no project.
Answer openingShapes(const RunContext&) {
    Json shapes = Json::array();
    for (const OpeningShape* shape : allOpeningShapes()) {
        Json entry = Json::object();
        entry["id"] = std::string(shape->id());
        entry["name"] = std::string(shape->name());
        entry["built_in"] = shape->builtIn();
        entry["size_rule"] = std::string(shape->sizeRule());
        shapes.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["shapes"] = std::move(shapes);
    return success("opening shapes", std::move(result));
}

}

std::vector<CommandSpec> openingCommands() {
    CommandSpec add;
    add.words = {"opening", "add"};
    add.description = "Add a door, window or plain opening on a wall.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AddOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--wall", options->wall, "Id of the wall. Use with --offset.")->type_name("id");
        sub.add_option("--offset", options->offset,
                       "Distance from the wall's first point to the centre of the opening, along the wall, in metres.")
            ->type_name("metres");
        sub.add_option("--x", options->x, "Place on the plan, to the right, in metres. Use with --y.")
            ->type_name("metres");
        sub.add_option("--y", options->y, "Place on the plan, down, in metres. Use with --x.")->type_name("metres");
        sub.add_option("--snap", options->snap, "How far a wall may be from --x and --y, in metres. Default: 0.3.")
            ->type_name("metres");
        sub.add_option("--step", options->step, "Round the offset to a multiple of this, in metres. Default: 0.")
            ->type_name("metres");
        sub.add_option("--floor", options->floor, "Floor id for --x and --y. Default: the first floor.")
            ->type_name("id");
        sub.add_option("--type", options->type, "Opening type. Default: door.")
            ->type_name("door|window|plain")
            ->check(CLI::IsMember({"door", "window", "plain"}));
        sub.add_option("--shape", options->shape, "Shape id. Run 'opening shapes' to list them. Default: rectangle.")
            ->type_name("id");
        sub.add_option("--width", options->width, "Width in metres. Default: from the project defaults.")
            ->type_name("metres");
        sub.add_option("--height", options->height, "Height in metres. Default: from the project defaults.")
            ->type_name("metres");
        sub.add_option("--sill", options->sill, "Height of the bottom above the floor in metres.")
            ->type_name("metres");
        sub.add_option("--hinge", options->hinge, "The jamb a door hangs on. Default: start.")
            ->type_name("start|end")
            ->check(CLI::IsMember({"start", "end"}));
        sub.add_option("--swing", options->swing, "The side of the wall a door opens toward. Default: right.")
            ->type_name("left|right")
            ->check(CLI::IsMember({"left", "right"}));
        return [options](const RunContext& context) { return openingAdd(context, *options); };
    };

    CommandSpec move;
    move.words = {"opening", "move"};
    move.description = "Move an opening along its wall, up or down, or onto another wall.";
    move.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<MoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the opening.")->type_name("id")->required();
        sub.add_option("--wall", options->wall, "Id of the wall to move it onto.")->type_name("id");
        sub.add_option("--offset", options->offset, "New distance along the wall to its centre, in metres.")
            ->type_name("metres");
        sub.add_option("--sill", options->sill, "New height of the bottom above the floor, in metres.")
            ->type_name("metres");
        return [options](const RunContext& context) { return openingMove(context, *options); };
    };

    CommandSpec resize;
    resize.words = {"opening", "resize"};
    resize.description = "Change the width or height of an opening.";
    resize.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ResizeOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the opening.")->type_name("id")->required();
        sub.add_option("--width", options->width, "New width in metres.")->type_name("metres");
        sub.add_option("--height", options->height, "New height in metres.")->type_name("metres");
        sub.add_option("--offset", options->offset, "New distance along the wall to its centre, in metres.")
            ->type_name("metres");
        sub.add_option("--sill", options->sill, "New height of the bottom above the floor, in metres.")
            ->type_name("metres");
        return [options](const RunContext& context) { return openingResize(context, *options); };
    };

    CommandSpec set;
    set.words = {"opening", "set"};
    set.description = "Change the type, shape, hinge, swing or door or window asset of an opening.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the opening.")->type_name("id")->required();
        sub.add_option("--type", options->type, "New type.")
            ->type_name("door|window|plain")
            ->check(CLI::IsMember({"door", "window", "plain"}));
        sub.add_option("--shape", options->shape, "New shape id. Run 'opening shapes' to list them.")
            ->type_name("id");
        sub.add_option("--hinge", options->hinge, "The jamb a door hangs on.")
            ->type_name("start|end")
            ->check(CLI::IsMember({"start", "end"}));
        sub.add_option("--swing", options->swing, "The side of the wall a door opens toward.")
            ->type_name("left|right")
            ->check(CLI::IsMember({"left", "right"}));
        sub.add_option("--frame", options->frame,
                       "The door or window that fills it: a catalog asset id, auto for one the build picks, or "
                       "generated for the fill Plynth builds.")
            ->type_name("asset|auto|generated");
        addLibraryOption(sub, options->library);
        return [options](const RunContext& context) { return openingSet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"opening", "remove"};
    remove.description = "Remove an opening.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the opening.")->type_name("id")->required();
        return [options](const RunContext& context) { return openingRemove(context, *options); };
    };

    CommandSpec list;
    list.words = {"opening", "list"};
    list.description = "List the openings of a floor and how many do not fit.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        sub.add_option("--wall", options->wall, "Only the openings on this wall.")->type_name("id");
        return [options](const RunContext& context) { return openingList(context, *options); };
    };

    CommandSpec shapes;
    shapes.words = {"opening", "shapes"};
    shapes.description = "List the opening shapes.";
    shapes.setup = [](CLI::App&) -> Handler { return openingShapes; };

    // Commands that read shapes see the custom shapes of the library first.
    for (CommandSpec* spec : {&add, &set, &list, &shapes}) {
        const auto inner = spec->setup;
        spec->setup = [inner](CLI::App& sub) -> Handler {
            const Handler handler = inner(sub);
            return [handler](const RunContext& context) {
                registerLibraryShapes();
                return handler(context);
            };
        };
    }

    return {add, move, resize, set, remove, list, shapes};
}

}
