#include "Common.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>

namespace plynth::cli {

void addProjectOption(CLI::App& sub, std::string& value) {
    sub.add_option("--project", value, "Project folder. Default: the current folder.")->type_name("folder");
}

std::filesystem::path resolveProjectFolder(const RunContext& context, const std::string& projectOption) {
    std::filesystem::path folder = context.currentFolder;
    if (!projectOption.empty()) {
        folder = context.currentFolder / pathFromUtf8(projectOption);
    }
    folder = folder.lexically_normal();
    if (!folder.has_filename() && folder.has_parent_path()) {
        folder = folder.parent_path();
    }
    return folder;
}

bool checkLock(const std::string& command, const std::filesystem::path& folder, bool readOnly, Answer& failed) {
    const Status allowed = allowCliCommand(folder, sessionNow(), readOnly);
    if (!allowed.ok()) {
        failed = failure(command, allowed);
        return false;
    }
    return true;
}

bool openProject(const std::string& command, const std::filesystem::path& folder, Document& document, Answer& failed,
                 bool readOnly) {
    if (!checkLock(command, folder, readOnly, failed)) {
        return false;
    }
    const Status status = document.open(folder);
    if (!status.ok()) {
        failed = failure(command, status);
        return false;
    }
    return true;
}

Status saveInPlace(const Document& document) {
    Project copy = document.project();
    return saveProject(copy, document.folder(), document.folder());
}

Json projectInfoJson(const std::filesystem::path& folder, const Project& project) {
    Json plan = Json::object();
    plan["version"] = 1;
    if (project.plan.scale.pixelsPerMetre) {
        plan["pixels_per_metre"] = *project.plan.scale.pixelsPerMetre;
    } else {
        plan["pixels_per_metre"] = nullptr;
    }
    Json floors = Json::array();
    for (const Floor& floor : project.plan.floors) {
        Json item = Json::object();
        item["id"] = floor.id;
        item["name"] = floor.name;
        bool found = false;
        if (floor.image) {
            item["image"] = *floor.image;
            std::error_code ignored;
            found = std::filesystem::exists(resolveImagePath(folder, *floor.image), ignored);
        } else {
            item["image"] = nullptr;
        }
        item["image_found"] = found;
        item["ceiling_height"] = floor.ceilingHeight;
        item["points"] = floor.points.size();
        item["walls"] = floor.walls.size();
        item["rooms"] = floor.rooms.size();
        item["openings"] = floor.openings.size();
        floors.push_back(std::move(item));
    }
    plan["floors"] = std::move(floors);

    Json result = Json::object();
    result["folder"] = pathToUtf8(folder);
    result["name"] = project.settings.name;
    result["export_mode"] = std::string(exportModeName(project.settings.exportMode));
    result["grid_size"] = project.settings.gridSize;
    result["has_autosave"] = findAutosave(folder).exists;
    result["plan"] = std::move(plan);
    return result;
}

std::string defaultFloorId(const Plan& plan) {
    return plan.floors.empty() ? std::string() : plan.floors.front().id;
}

}
