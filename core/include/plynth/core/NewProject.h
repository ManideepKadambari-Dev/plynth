#pragma once

#include "AgentFiles.h"
#include "Project.h"
#include "Status.h"

#include <filesystem>
#include <string_view>

namespace plynth {

// Making a new project, the same from the app's New project dialog, a sample plan and "plynth project new"
// (issues 33 and 43). The folder rules were the app's; they are core's now, so both refuse the same folders.

// Checks a project name as a Windows folder name. InvalidValue with the app's sentences: empty ("Type a name for
// the project."), a character of \ / : * ? " < > | or a control character, a name ending with a dot or a space,
// or a name Windows keeps for itself (CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9, in any letter case, also
// with an ending such as "nul.txt").
Status checkProjectName(std::string_view name);

// Checks the folder a new project would get: location/name. checkProjectName first; then InvalidValue when the
// location is empty, not absolute or a file; AlreadyExists when location/name is a file, a folder that holds a
// project, or a folder with anything in it (hidden and system files count). A location that does not exist yet
// is fine: createNewProject makes it.
Status checkNewProjectFolder(const std::filesystem::path& location, std::string_view name);

// The same for a whole folder path, as "plynth project new --folder" gives it: its parent is the location and its
// last part the name.
Status checkNewProjectFolder(const std::filesystem::path& folder);

struct NewProjectOptions {
    bool agentGuide = true; // write AGENTS.md; "plynth project new --no-agent-guide" turns it off
};

struct NewProjectResult {
    Status status;             // the first of checkNewProjectFolder and createProject (ProjectFiles.h)
    AgentFileWrite agentGuide; // Kept with Ok status when switched off; a failed guide never fails the project
};

// Checks the folder, creates the project there (createProject) and writes AGENTS.md (writeAgentGuide, never
// overwriting one). The project is opened by the caller only after this returns Ok.
NewProjectResult createNewProject(Project& project, const std::filesystem::path& folder,
                                  const std::filesystem::path& sourceFolder, const NewProjectOptions& options = {});

}
