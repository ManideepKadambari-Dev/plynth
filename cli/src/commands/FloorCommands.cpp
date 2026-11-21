#include "../Common.h"

namespace plynth::cli {

namespace {

struct ListOptions {
    std::string project;
};

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
    return {list};
}

}
