#include "../Common.h"

#include <plynth/core/commands/FloorCommands.h>

#include <memory>
namespace plynth::cli {

namespace {

struct ListOptions {
    std::string project;
};

struct SetOptions {
    std::string project;
    std::string floor;
    double ceilingHeight = 0.0;
};

// Sets the ceiling height of a floor.
Answer floorSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "floor set";
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
        if (floor == nullptr) {
            return failed;
        }
        const Id id = floor->id;
        if (!runCommand(command, document, std::make_unique<SetCeilingHeightCommand>(id, options.ceilingHeight),
                        failed)) {
            return failed;
        }
        return success(command, floorEntry(*findFloor(document.plan(), id)));
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
        floors.push_back(floorEntry(floor));
    }
    Json result = Json::object();
    result["floors"] = std::move(floors);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> floorCommands() {
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
    set.description = "Set the ceiling height of a floor.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        sub.add_option("--ceiling-height", options->ceilingHeight, "Ceiling height in metres.")
            ->type_name("metres")
            ->required();
        return [options](const RunContext& context) { return floorSet(context, *options); };
    };

    return {list, set};
}

}
