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

std::vector<CommandSpec> projectCommands() {
    std::vector<CommandSpec> specs;
    for (const char* name : {"new", "info"}) {
        CommandSpec spec;
        spec.words = {"project", name};
        spec.description = "Not available yet.";
        spec.setup = [words = joinWords(spec.words)](CLI::App&) { return notBuilt(words); };
        specs.push_back(std::move(spec));
    }
    return specs;
}

}
