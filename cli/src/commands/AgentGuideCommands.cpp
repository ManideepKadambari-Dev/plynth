#include "../Common.h"

#include <plynth/core/AgentFiles.h>
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

struct SkillOptions {
    std::string out;
    bool force = false;
};

// The answer for a written or kept file, with a warning when a file that was there was kept.
Answer writtenAnswer(const std::string& command, const AgentFileWrite& written, const char* key) {
    Json result = Json::object();
    result["file"] = pathToUtf8(written.file);
    const bool kept = written.result == AgentFileResult::Kept;
    result[key] = kept ? "kept" : "written";
    Answer answer = success(command, std::move(result));
    if (kept) {
        answer.warnings.push_back(AnswerWarning{
            std::string(errorCodeName(ErrorCode::AlreadyExists)),
            "The file " + pathToUtf8(written.file) + " is already there and was kept. Give --force to replace it."});
    }
    return answer;
}

// Writes AGENTS.md into an existing project, keeping one that is there unless --force is given.
Answer agentGuide(const RunContext& context, const AgentGuideOptions& options) {
    const std::string command = "agent-guide";
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    if (!isProjectFolder(folder)) {
        return failure(command, fail(ErrorCode::NotAProject, "The folder " + pathToUtf8(folder) + " holds no project."));
    }
    const AgentFileWrite written = writeAgentGuide(folder, options.force);
    if (!written.status.ok()) {
        return failure(command, written.status);
    }
    return writtenAnswer(command, written, "agent_guide");
}

// Writes the skill file into <folder>/plynth/SKILL.md, keeping one that is there unless --force is given.
Answer skillInstall(const RunContext& context, const SkillOptions& options) {
    const std::string command = "skill install";
    if (options.out.empty()) {
        return failure(command, fail(ErrorCode::InvalidValue, "Give the skills folder with --out."));
    }
    const std::filesystem::path folder = (context.currentFolder / pathFromUtf8(options.out)).lexically_normal();
    const AgentFileWrite written = installSkill(folder, options.force);
    if (!written.status.ok()) {
        return failure(command, written.status);
    }
    return writtenAnswer(command, written, "skill");
}

}

std::vector<CommandSpec> agentGuideCommands() {
    CommandSpec guide;
    guide.words = {"agent-guide"};
    guide.description = "Write the instructions file AGENTS.md into a project, with the rules and the check loop for a tool that works on it.";
    guide.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<AgentGuideOptions>();
        addProjectOption(sub, options->project);
        sub.add_flag("--force", options->force, "Replace an AGENTS.md that is already there.");
        return [options](const RunContext& context) { return agentGuide(context, *options); };
    };

    CommandSpec skill;
    skill.words = {"skill", "install"};
    skill.description = "Write the Plynth skill file into <folder>/plynth/SKILL.md, for a tool that reads skills from that folder.";
    skill.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SkillOptions>();
        sub.add_option("--out", options->out, "The skills folder. It must exist.")->type_name("folder")->required();
        sub.add_flag("--force", options->force, "Replace a SKILL.md that is already there, for example after an update.");
        return [options](const RunContext& context) { return skillInstall(context, *options); };
    };
    return {guide, skill};
}

}
