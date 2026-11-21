#include "../Registry.h"

namespace plynth::cli {

namespace {

// Placeholder answer for a command that is declared but not built yet.
Handler notBuilt(std::string words) {
    return [words](const RunContext&) {
        return badArguments(words, "unknown_command", "The command '" + words + "' is not available yet.");
    };
}

}

std::vector<CommandSpec> editCommands() {
    std::vector<CommandSpec> specs;
    for (const char* name : {"scale set", "point add", "wall add"}) {
        CommandSpec spec;
        const std::string text = name;
        const auto space = text.find(' ');
        spec.words = {text.substr(0, space), text.substr(space + 1)};
        spec.description = "Not available yet.";
        spec.setup = [words = joinWords(spec.words)](CLI::App&) { return notBuilt(words); };
        specs.push_back(std::move(spec));
    }
    return specs;
}

}
