#include "../Common.h"

#include <plynth/core/OpeningShapes.h>

namespace plynth::cli {

namespace {

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
    CommandSpec shapes;
    shapes.words = {"opening", "shapes"};
    shapes.description = "List the opening shapes.";
    shapes.setup = [](CLI::App&) -> Handler { return openingShapes; };
    return {shapes};
}

}
