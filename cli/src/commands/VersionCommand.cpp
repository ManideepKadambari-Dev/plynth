#include "../Registry.h"

#include <plynth/core/ProjectJson.h>
#include <plynth/core/Version.h>

namespace plynth::cli {

namespace {

// Answers with the program version and the file format versions it reads and writes.
Answer versionAnswer(const RunContext&) {
    Json result = Json::object();
    result["version"] = std::string(version());
    result["plan_format"] = kPlanFormatVersion;
    result["project_format"] = kProjectFormatVersion;
    return success("version", std::move(result));
}

}

std::vector<CommandSpec> versionCommands() {
    CommandSpec spec;
    spec.words = {"version"};
    spec.description = "Show the Plynth version and the file format versions.";
    spec.setup = [](CLI::App&) -> Handler { return versionAnswer; };
    return {spec};
}

}
