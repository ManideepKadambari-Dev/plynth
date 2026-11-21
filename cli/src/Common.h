#pragma once

#include "Registry.h"

#include <plynth/core/Document.h>

#include <filesystem>
#include <string>

namespace plynth::cli {

// Adds the --project option, which names the project folder. Without it the current folder is used.
void addProjectOption(CLI::App& sub, std::string& value);

// The project folder to work on: the --project value, or the current folder. Absolute, forward slashes
// when printed.
std::filesystem::path resolveProjectFolder(const RunContext& context, const std::string& projectOption);

// Checks the edit lock (allowCliCommand) and then opens the project in a folder into a document.
// readOnly true is for commands that only read. On error the answer for command is returned in failed.
bool openProject(const std::string& command, const std::filesystem::path& folder, Document& document, Answer& failed,
                 bool readOnly = false);

// Checks the edit lock for a command that reads the project without a document. Returns false and
// fills failed when the command is refused.
bool checkLock(const std::string& command, const std::filesystem::path& folder, bool readOnly, Answer& failed);

// Saves the document's project into its own folder, leaving any autosave alone.
Status saveInPlace(const Document& document);

// Describes a project for project info and project new.
Json projectInfoJson(const std::filesystem::path& folder, const Project& project);

// The floor that commands use when the user gave none: the first one. Empty when there is none.
std::string defaultFloorId(const Plan& plan);

}
