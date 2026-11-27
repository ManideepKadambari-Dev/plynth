#pragma once

#include "Status.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace plynth {

// The instructions Plynth hands to a coding assistant the user runs themselves (docs/AI_INTEGRATION.md):
//   AGENTS.md  in a project folder, for an assistant started there. Written by every new project, from the app or
//              the CLI (NewProject.h), and by "plynth agent-guide". An AGENTS.md that is there is never
//              overwritten unless asked.
//   SKILL.md   in the open Agent Skills format, installed once into the skills folder the user's assistant reads,
//              so it can start a Plynth project in an empty folder. The installer puts a copy in the Plynth
//              folder; "plynth skill install --out <folder>" and the app's "Set up my coding assistant" button
//              write <folder>/plynth/SKILL.md.
// Both come from docs/templates/ (resourceFolder(ResourceKind::Docs)): project-agents.md and SKILL.md. The part
// they share sits in each between the lines "<!-- plynth-shared-begin -->" and "<!-- plynth-shared-end -->"
// and must be the same text in both; a test checks it.

inline constexpr std::string_view kAgentGuideFileName = "AGENTS.md";
inline constexpr std::string_view kSkillFileName = "SKILL.md";
inline constexpr std::string_view kSkillFolderName = "plynth";
inline constexpr std::string_view kAgentGuideTemplate = "templates/project-agents.md";
inline constexpr std::string_view kSkillTemplate = "templates/SKILL.md";
inline constexpr std::string_view kSharedBeginLine = "<!-- plynth-shared-begin -->";
inline constexpr std::string_view kSharedEndLine = "<!-- plynth-shared-end -->";

enum class AgentFileResult { Written, Kept };

struct AgentFileWrite {
    Status status; // NotFound when the template is not next to the program, CannotWrite
    AgentFileResult result = AgentFileResult::Kept;
    std::filesystem::path file; // absolute
};

// Copies the AGENTS.md template into a project folder. An existing file is kept unless overwrite is true.
AgentFileWrite writeAgentGuide(const std::filesystem::path& projectFolder, bool overwrite);

// Writes skillsFolder/plynth/SKILL.md from the template, creating the folder. An existing file is kept unless
// force is true (to refresh it after an update). NotFound when skillsFolder does not exist.
AgentFileWrite installSkill(const std::filesystem::path& skillsFolder, bool force);

// The text between the shared lines of a template, for the test that both agree. Empty when the lines are
// missing or out of order.
std::string sharedAgentText(std::string_view templateText);

}
