#pragma once

#include "Project.h"
#include "Status.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Project folder layout (DESIGN.md section 5):
//   project.json        settings
//   plan.json           the plan
//   plans/              plan images
//   autosave/           project.json and plan.json with unsaved changes, removed after a save
//   session.lock        the CLI edit session, see EditSession.h
//   app-state.json      written by the app while it has the project open, see EditSession.h

// How often the app writes an autosave while there are unsaved changes.
inline constexpr int kAutosaveIntervalSeconds = 30;

// Turns UTF-8 text into a path. Use this instead of std::filesystem::path(std::string), which reads
// the text in the Windows ANSI code page.
std::filesystem::path pathFromUtf8(std::string_view text);

// Turns a path into UTF-8 text with forward slashes.
std::string pathToUtf8(const std::filesystem::path& path);

// Where a floor image lives on disk: absolute images as they are, relative ones inside the project folder.
std::filesystem::path resolveImagePath(const std::filesystem::path& projectFolder, std::string_view image);

// Says whether a folder holds a project, which means it has a project.json file.
bool isProjectFolder(const std::filesystem::path& folder);

// The answer from opening a project folder.
struct ProjectLoad {
    Status status;
    Project project;
    bool migrated = false; // true when either file was upgraded from an older version
};

// Reads project.json and plan.json from a project folder. NotAProject when project.json is missing,
// NotFound when plan.json is missing, otherwise the errors of settingsFromJson and planFromJson, with
// the file name at the start of the message. Never writes anything, even after an upgrade.
ProjectLoad loadProject(const std::filesystem::path& folder);

// What a save noticed without refusing it.
struct SaveReport {
    // Image paths, as written in plan.json, of floors whose image file could not be found. They were not
    // copied and their paths were kept as they were, so the image shows again once the file is put back.
    std::vector<std::string> missingImages;
};

// Saves a project into a folder, creating the folder and plans/ when needed.
// First every floor image is resolved against sourceFolder (empty for a project that was never saved).
// Images that are not already inside folder/plans/ are copied there, keeping their file name and adding
// "-2", "-3" and so on when another file has that name. The image paths in project are then rewritten
// to "plans/<name>". Then plan.json and project.json are each written to a temporary file and renamed over
// the old one, so a failed save never leaves half a file. Images are copied before any JSON is written:
// a failed copy returns CannotWrite and leaves both JSON files as they were. An image that cannot be found
// does not stop the save: it is skipped, its path is kept unchanged and it is listed in report, when given.
Status saveProject(Project& project, const std::filesystem::path& folder,
                   const std::filesystem::path& sourceFolder, SaveReport* report = nullptr);

// Same as saveProject, but refuses with AlreadyExists when the folder already holds a project, and with
// NotFound when a floor image cannot be found, since a new project must start from a real image.
Status createProject(Project& project, const std::filesystem::path& folder,
                     const std::filesystem::path& sourceFolder);

// What is known about a project's autosave.
struct AutosaveInfo {
    bool exists = false;
    std::filesystem::file_time_type time{};
    bool olderThanSaved = false; // plan.json was written after the autosave
};

// Looks for folder/autosave/plan.json and folder/autosave/project.json.
AutosaveInfo findAutosave(const std::filesystem::path& folder);

// Writes the project into folder/autosave/ in the same format as a normal save. Images are not copied,
// and image paths stay relative to the project folder, not to the autosave folder.
Status writeAutosave(const Project& project, const std::filesystem::path& folder);

// Reads the autosave of a project folder, with the same checks as loadProject.
ProjectLoad loadAutosave(const std::filesystem::path& folder);

// Deletes folder/autosave/. Ok when there is nothing to delete.
Status removeAutosave(const std::filesystem::path& folder);

}
