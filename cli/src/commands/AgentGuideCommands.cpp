#include "../AgentGuide.h"
#include "../Common.h"

#include <plynth/core/ProjectFiles.h>

#include <memory>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct AgentGuideOptions {
    std::string project;
    bool force = false;
};

// Writes AGENTS.md into an existing project, or refreshes it with --force.
Answer agentGuide(const RunContext& context, const AgentGuideOptions& options) {
    const std::string command = "agent-guide";
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    if (!isProjectFolder(folder)) {
        return failure(command, fail(ErrorCode::NotAProject, "The folder " + pathToUtf8(folder) + " holds no project."));
    }
    const std::filesystem::path file = folder / kAgentGuideFileName;
    std::error_code ec;
    if (!options.force && std::filesystem::exists(file, ec)) {
        return failure(command, fail(ErrorCode::AlreadyExists,
                                     "The file " + pathToUtf8(file) + " already exists. Give --force to replace it."));
    }
    const AgentGuideWrite written = writeAgentGuide(folder, options.force);
    if (!written.status.ok()) {
        return failure(command, written.status);
    }
    Json result = Json::object();
    result["file"] = pathToUtf8(file);
    result["agent_guide"] = "written";
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> agentGuideCommands() {
    CommandSpec spec;
    spec.words = {"agent-guide"};
    spec.description = "Write the instructions file AGENTS.md into a project, with the rules and the check loop for a tool that works on it.";
    spec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AgentGuideOptions>();
        addProjectOption(sub, options->project);
        sub.add_flag("--force", options->force, "Replace an AGENTS.md that is already there.");
        return [options](const RunContext& context) { return agentGuide(context, *options); };
    };
    return {spec};
}

}
