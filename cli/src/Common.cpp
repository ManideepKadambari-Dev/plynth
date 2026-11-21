#include "Common.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/ProjectJson.h>

#include <algorithm>

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
    plan["version"] = kPlanFormatVersion;
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

Answer editProject(const std::string& command, const RunContext& context, const std::string& projectOption,
                   const std::function<Answer(Document&)>& body) {
    const std::filesystem::path folder = resolveProjectFolder(context, projectOption);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed)) {
        return failed;
    }
    Answer answer = body(document);
    if (!answer.ok) {
        return answer;
    }
    const Status saved = saveInPlace(document);
    if (!saved.ok()) {
        return failure(command, saved);
    }
    return answer;
}

bool runCommand(const std::string& command, Document& document, std::unique_ptr<Command> toRun, Answer& failed) {
    const Status run = document.execute(std::move(toRun));
    if (!run.ok()) {
        failed = failure(command, run);
        return false;
    }
    return true;
}

const Floor* resolveFloor(const std::string& command, const Plan& plan, const std::string& floorOption,
                          Answer& failed) {
    const std::string id = floorOption.empty() ? defaultFloorId(plan) : floorOption;
    const Floor* floor = findFloor(plan, id);
    if (floor == nullptr) {
        failed = failure(command, fail(ErrorCode::NotFound, "There is no floor " + id + "."));
    }
    return floor;
}

Json pointEntry(const Floor& floor, const Point& point) {
    Json walls = Json::array();
    for (const Wall& wall : floor.walls) {
        if (wall.from == point.id || wall.to == point.id) {
            walls.push_back(wall.id);
        }
    }
    Json entry = Json::object();
    entry["id"] = point.id;
    entry["floor"] = floor.id;
    entry["x"] = point.x;
    entry["y"] = point.y;
    entry["corner_radius"] = point.cornerRadius;
    entry["walls"] = std::move(walls);
    return entry;
}

Json wallEntry(const Floor& floor, const Wall& wall) {
    Json rooms = Json::array();
    for (const Room& room : floor.rooms) {
        if (std::ranges::find(room.walls, wall.id) != room.walls.end()) {
            rooms.push_back(room.id);
        }
    }
    Json entry = Json::object();
    entry["id"] = wall.id;
    entry["floor"] = floor.id;
    entry["from"] = wall.from;
    entry["to"] = wall.to;
    entry["thickness"] = wall.thickness;
    if (wall.height) {
        entry["height"] = *wall.height;
    } else {
        entry["height"] = nullptr;
    }
    entry["bulge"] = wall.bulge;
    entry["length"] = wallLength(floor, wall);
    entry["rooms"] = std::move(rooms);
    return entry;
}

Answer removeCascade(const std::string& command, const std::string& kind, const RunContext& context,
                     const std::string& projectOption,
                     const Id& id) {
    return editProject(command, context, projectOption, [&](Document& document) {
        if (const Floor* floor = findFloorContaining(document.plan(), id)) {
            std::string actual = "item";
            if (findPoint(*floor, id) != nullptr) {
                actual = "point";
            } else if (findWall(*floor, id) != nullptr) {
                actual = "wall";
            } else if (findRoom(*floor, id) != nullptr) {
                actual = "room";
            } else if (findOpening(*floor, id) != nullptr) {
                actual = "opening";
            }
            if (actual != kind) {
                return failure(command, fail(ErrorCode::InvalidValue, id + " is a " + actual + ", not a " + kind +
                                                                          ". Use " + actual + " remove."));
            }
        }
        const std::vector<Id> named = {id};
        DeleteBuild build = makeDeleteCommand(document.plan(), named);
        if (!build.status.ok()) {
            return failure(command, build.status);
        }
        Answer failed;
        if (!runCommand(command, document, std::move(build.command), failed)) {
            return failed;
        }
        return success(command, removedJson(build.removes.rooms, build.removes.walls, build.removes.points));
    });
}

Json removedJson(const std::vector<Id>& rooms, const std::vector<Id>& walls, const std::vector<Id>& points) {
    Json removed = Json::object();
    removed["rooms"] = rooms;
    removed["walls"] = walls;
    removed["points"] = points;
    Json result = Json::object();
    result["removed"] = std::move(removed);
    return result;
}

}
