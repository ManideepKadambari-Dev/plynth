#include "../Common.h"

#include <plynth/core/PlanImage.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/commands/FloorCommands.h>

#include <cmath>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct ListOptions {
    std::string project;
};

struct AddOptions {
    std::string project;
    std::string name;
    std::optional<double> ceilingHeight;
    std::optional<double> slabThickness;
    std::string image;
    std::string below;
};

struct SetOptions {
    std::string project;
    std::string floor;
    std::string name;
    std::optional<double> ceilingHeight;
    std::optional<double> slabThickness;
    std::string image;
    std::string imageOffset;
    bool keepMarks = false;
};

struct RemoveOptions {
    std::string project;
    std::string id;
    bool yes = false;
};

// Reads "x,y" into an offset. False for anything else.
bool parseOffset(const std::string& text, ImageOffset& offset) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    try {
        std::size_t used = 0;
        const std::string first = text.substr(0, comma);
        const std::string second = text.substr(comma + 1);
        offset.x = std::stod(first, &used);
        if (used != first.size()) {
            return false;
        }
        offset.y = std::stod(second, &used);
        return used == second.size() && std::isfinite(offset.x) && std::isfinite(offset.y);
    } catch (const std::exception&) {
        return false;
    }
}

// Checks an image file given on the command line and returns its absolute path.
bool checkImage(const std::string& command, const RunContext& context, const std::string& option,
                std::string& stored, Answer& failed) {
    const std::filesystem::path path = (context.currentFolder / pathFromUtf8(option)).lexically_normal();
    const PlanImageProbe probe = probePlanImage(path);
    if (!probe.ok()) {
        failed = failure(command, fail(ErrorCode::ImageError, probe.message));
        return false;
    }
    stored = pathToUtf8(path);
    return true;
}

// After a save that copied an image into the project, the entry shows the stored path, so it is read again.
// The backup list of the save stays in the answer.
Answer withSavedEntry(Answer answer, const RunContext& context, const std::string& project, const Id& id) {
    if (!answer.ok) {
        return answer;
    }
    Document document;
    Answer failed;
    if (!openProject(answer.command, resolveProjectFolder(context, project), document, failed, true)) {
        return answer;
    }
    if (const Floor* floor = findFloor(document.plan(), id)) {
        Json entry = floorEntry(document.plan(), *floor);
        if (answer.result.is_object() && answer.result.contains("backup")) {
            entry["backup"] = answer.result.at("backup");
        }
        answer.result = std::move(entry);
    }
    return answer;
}

// Adds an empty floor on top, or below one floor.
Answer floorAdd(const RunContext& context, const AddOptions& options) {
    const std::string command = "floor add";
    std::string image;
    if (!options.image.empty()) {
        Answer failed;
        if (!checkImage(command, context, options.image, image, failed)) {
            return failed;
        }
    }
    Id addedId;
    Answer answer = editProject(command, context, options.project, [&](Document& document) {
        const Plan& plan = document.plan();
        std::optional<std::size_t> position;
        if (!options.below.empty()) {
            position = floorIndex(plan, options.below);
            if (!position) {
                return failure(command, fail(ErrorCode::NotFound, "There is no floor " + options.below + "."));
            }
        }
        const std::string name = options.name.empty() ? "Floor " + std::to_string(plan.floors.size() + 1) : options.name;
        auto add = std::make_unique<AddFloorCommand>(name, position);
        const AddFloorCommand* raw = add.get();
        Answer failed;
        if (!runCommand(command, document, std::move(add), failed)) {
            return failed;
        }
        const Id newId = raw->newFloorId();
        addedId = newId;
        auto more = std::make_unique<CommandGroup>("Set new floor");
        bool any = false;
        if (options.ceilingHeight) {
            more->add(std::make_unique<SetCeilingHeightCommand>(newId, *options.ceilingHeight));
            any = true;
        }
        if (options.slabThickness) {
            more->add(std::make_unique<SetSlabThicknessCommand>(newId, *options.slabThickness));
            any = true;
        }
        if (!image.empty()) {
            more->add(std::make_unique<SetFloorImageCommand>(newId, image));
            any = true;
        }

        if (any && !runCommand(command, document, std::move(more), failed)) {
            return failed;
        }
        return success(command, floorEntry(document.plan(), *findFloor(document.plan(), newId)));
    });
    return image.empty() ? answer : withSavedEntry(std::move(answer), context, options.project, addedId);
}

// Changes a floor: name, heights, plan image and image offset, as one step.
Answer floorSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "floor set";
    if (options.name.empty() && !options.ceilingHeight && !options.slabThickness && options.image.empty() &&
        options.imageOffset.empty()) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --name, --ceiling-height, --slab-thickness, --image and "
                            "--image-offset.");
    }
    ImageOffset offset;
    if (!options.imageOffset.empty() && !parseOffset(options.imageOffset, offset)) {
        return badArguments(command, "bad_arguments", "The image offset must be two numbers in metres, as x,y.");
    }
    std::string image;
    if (!options.image.empty() && options.image != "none") {
        Answer failed;
        if (!checkImage(command, context, options.image, image, failed)) {
            return failed;
        }
    }
    Id changedId;
    Answer answer = editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
        if (floor == nullptr) {
            return failed;
        }
        const Id id = floor->id;
        changedId = id;
        auto group = std::make_unique<CommandGroup>("Change floor");
        if (!options.name.empty()) {
            group->add(std::make_unique<SetFloorNameCommand>(id, options.name));
        }
        if (options.ceilingHeight) {
            group->add(std::make_unique<SetCeilingHeightCommand>(id, *options.ceilingHeight));
        }
        if (options.slabThickness) {
            group->add(std::make_unique<SetSlabThicknessCommand>(id, *options.slabThickness));
        }
        if (options.image == "none") {
            group->add(std::make_unique<SetFloorImageCommand>(id, std::nullopt));
        } else if (!image.empty()) {
            group->add(std::make_unique<SetFloorImageCommand>(id, image));
        }
        if (!options.imageOffset.empty()) {
            group->add(std::make_unique<SetFloorImageOffsetCommand>(id, offset, !options.keepMarks));
        }
        if (!runCommand(command, document, std::move(group), failed)) {
            return failed;
        }
        return success(command, floorEntry(document.plan(), *findFloor(document.plan(), id)));
    });
    return image.empty() ? answer : withSavedEntry(std::move(answer), context, options.project, changedId);
}

// Removes a floor with everything on it. The answer lists stairs left without a floor above them.
Answer floorRemove(const RunContext& context, const RemoveOptions& options) {
    const std::string command = "floor remove";
    if (!options.yes) {
        return badArguments(command, "bad_arguments",
                            "Removing a floor deletes everything on it. Add --yes to confirm.");
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        if (!runCommand(command, document, std::make_unique<RemoveFloorCommand>(options.id), failed)) {
            return failed;
        }
        Json orphans = Json::array();
        for (const Floor& floor : document.plan().floors) {
            for (const StairProblem& problem : stairProblems(document.plan(), floor)) {
                if (problem.code == "stair_no_floor_above") {
                    orphans.push_back(problem.stair);
                }
            }
        }
        Json result = Json::object();
        result["removed"] = options.id;
        result["stairs_without_floor"] = std::move(orphans);
        return success(command, std::move(result));
    });
}

// Lists the floors of the project.
Answer floorList(const RunContext& context, const ListOptions& options) {
    const std::string command = "floor list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    Json floors = Json::array();
    for (const Floor& floor : document.plan().floors) {
        floors.push_back(floorEntry(document.plan(), floor));
    }
    Json result = Json::object();
    result["floors"] = std::move(floors);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> floorCommands() {
    CommandSpec add;
    add.words = {"floor", "add"};
    add.description = "Add a floor on top, or below one floor with --below.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AddOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--name", options->name, "Floor name. Default: Floor N.");
        sub.add_option("--ceiling-height", options->ceilingHeight, "Ceiling height in metres. Default: the project's.")
            ->type_name("metres");
        sub.add_option("--slab-thickness", options->slabThickness, "Slab thickness above the floor in metres.")
            ->type_name("metres");
        sub.add_option("--image", options->image, "Plan image for the floor (PNG, JPEG or BMP).")->type_name("file");
        sub.add_option("--below", options->below, "Put the new floor right below this floor.")->type_name("id");
        return [options](const RunContext& context) { return floorAdd(context, *options); };
    };
    CommandSpec remove;
    remove.words = {"floor", "remove"};
    remove.description = "Remove a floor and everything on it. Needs --yes.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Floor id.")->type_name("id")->required();
        sub.add_flag("--yes", options->yes, "Confirm the removal.");
        return [options](const RunContext& context) { return floorRemove(context, *options); };
    };
    CommandSpec list;
    list.words = {"floor", "list"};
    list.description = "List the floors of the project.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        return [options](const RunContext& context) { return floorList(context, *options); };
    };
    CommandSpec set;
    set.words = {"floor", "set"};
    set.description = "Change the name, heights, plan image or image offset of a floor.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        sub.add_option("--name", options->name, "New name.");
        sub.add_option("--ceiling-height", options->ceilingHeight, "Ceiling height in metres.")->type_name("metres");
        sub.add_option("--slab-thickness", options->slabThickness, "Slab thickness above the floor in metres.")
            ->type_name("metres");
        sub.add_option("--image", options->image, "Plan image file, or none to clear it.")->type_name("file");
        sub.add_option("--image-offset", options->imageOffset, "Where the image sits on the plan, as x,y in metres.")
            ->type_name("x,y");
        sub.add_flag("--keep-marks", options->keepMarks, "With --image-offset, leave the marks where they are.");
        return [options](const RunContext& context) { return floorSet(context, *options); };
    };

    return {add, remove, list, set};
}

}
