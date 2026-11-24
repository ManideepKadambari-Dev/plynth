#include "Common.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/Openings.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Storeys.h>

#include <algorithm>
#include <cmath>
#include <optional>

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

std::stop_token stopToken(const RunContext& context) {
    return context.host != nullptr ? context.host->stop : std::stop_token{};
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

Status saveInPlace(const Document& document, SaveReport* report) {
    Project copy = document.project();
    return saveProject(copy, document.folder(), document.folder(), report);
}

void addSaveWarnings(Answer& answer, const SaveReport& report) {
    for (const std::string& image : report.missingImages) {
        answer.warnings.push_back({"image_missing", "The plan image " + image + " is missing. Its path was kept."});
    }
}

namespace {

// The default sizes as they appear in project info.
Json defaultsJson(const DefaultSizes& sizes) {
    Json defaults = Json::object();
    defaults["ceiling_height"] = sizes.ceilingHeight;
    defaults["door_width"] = sizes.doorWidth;
    defaults["door_height"] = sizes.doorHeight;
    defaults["window_width"] = sizes.windowWidth;
    defaults["window_height"] = sizes.windowHeight;
    defaults["window_sill"] = sizes.windowSill;
    defaults["wall_item_centre"] = sizes.wallItemCentre;
    return defaults;
}

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
    result["defaults"] = defaultsJson(project.settings.defaults);
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
    SaveReport report;
    const Status saved = saveInPlace(document, &report);
    if (!saved.ok()) {
        return failure(command, saved);
    }
    addSaveWarnings(answer, report);
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
    entry["corner_radius_used"] = cornerRounding(floor, point).radius;
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
    Json onWall = Json::array();
    for (const Opening& opening : floor.openings) {
        if (opening.wall == wall.id) {
            onWall.push_back(opening.id);
        }
    }
    const WallCurve curve = wallCurve(floor, wall);
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
    if (curve.curved) {
        entry["radius"] = curve.radius;
    } else {
        entry["radius"] = nullptr;
    }
    entry["height_used"] = wallHeight(floor, wall);
    entry["rooms"] = std::move(rooms);
    entry["openings"] = std::move(onWall);
    return entry;
}

Json openingEntry(const Floor& floor, const Opening& opening) {
    const Status fit = checkOpeningFit(floor, opening);
    const OpeningPlacement placement = openingPlacement(floor, opening);
    Json centre = Json::object();
    centre["x"] = placement.centre.x;
    centre["y"] = placement.centre.y;
    Json entry = Json::object();
    entry["id"] = opening.id;
    entry["floor"] = floor.id;
    entry["wall"] = opening.wall;
    entry["offset"] = opening.offset;
    entry["type"] = std::string(openingTypeName(opening.type));
    entry["shape"] = opening.shape;
    entry["width"] = opening.width;
    entry["height"] = opening.height;
    entry["sill"] = opening.sill;
    entry["hinge"] = std::string(hingeEndName(opening.hinge));
    entry["swing"] = std::string(wallSideName(opening.swing));
    if (opening.frame) {
        entry["frame"] = *opening.frame;
    } else {
        entry["frame"] = nullptr;
    }
    entry["auto_frame"] = opening.autoFrame;
    entry["centre"] = std::move(centre);
    entry["fits"] = fit.ok();
    if (fit.ok()) {
        entry["problem"] = nullptr;
    } else {
        entry["problem"] = fit.message;
    }
    return entry;
}

Json floorEntry(const Plan& plan, const Floor& floor) {
    Json entry = Json::object();
    entry["id"] = floor.id;
    entry["name"] = floor.name;
    if (floor.image) {
        entry["image"] = *floor.image;
    } else {
        entry["image"] = nullptr;
    }
    entry["ceiling_height"] = floor.ceilingHeight;
    entry["slab_thickness"] = floor.slabThickness;
    Json offset = Json::object();
    offset["x"] = floor.imageOffset.x;
    offset["y"] = floor.imageOffset.y;
    entry["image_offset"] = std::move(offset);
    const std::optional<std::size_t> index = floorIndex(plan, floor.id);
    const std::vector<double> levels = floorElevations(plan);
    const double elevation = index && *index < levels.size() ? levels[*index] : 0.0;
    entry["elevation"] = std::round(elevation * 1e6) / 1e6;
    entry["rise"] = std::round(floorRise(floor) * 1e6) / 1e6;
    Json stairs = Json::array();
    for (const Stair& stair : floor.stairs) {
        stairs.push_back(stair.id);
    }
    entry["stairs"] = std::move(stairs);
    entry["roof_sections"] = roofAreasOf(plan, floor.id).size();
    return entry;
}

Answer removeCascade(const std::string& command, const std::string& kind, const RunContext& context,
                     const std::string& projectOption,
                     const Id& id) {
    return editProject(command, context, projectOption, [&](Document& document) {
        if (const Floor* floor = findFloorContaining(document.plan(), id)) {
            std::string actual = "item";
            if (findStair(*floor, id) != nullptr) {
                actual = "stair";
            } else if (findItem(*floor, id) != nullptr) {
                actual = "item";
            } else if (findPoint(*floor, id) != nullptr) {
                actual = "point";
            } else if (findWall(*floor, id) != nullptr) {
                actual = "wall";
            } else if (findRoom(*floor, id) != nullptr) {
                actual = "room";
            } else if (findOpening(*floor, id) != nullptr) {
                actual = "opening";
            } else if (findLight(*floor, id) != nullptr) {
                actual = "light";
            }
            if (actual != kind) {
                const auto article = [](const std::string& word) { return word == "item" || word == "opening" ? "an " : "a "; };
                return failure(command, fail(ErrorCode::InvalidValue, id + " is " + article(actual) + actual + ", not " +
                                                                          article(kind) + kind + ". Use " + actual +
                                                                          " remove."));
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
        return success(command, removedJson(build.removes.items, build.removes.detached, build.removes.openings,
                                            build.removes.rooms, build.removes.walls, build.removes.points));
    });
}

std::optional<Answer> refuseStairId(const std::string& command, const std::string& kind, const RunContext& context,
                                    const std::string& projectOption, const Id& id) {
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, projectOption), document, failed, true)) {
        return std::nullopt;
    }
    const Floor* floor = findFloorContaining(document.plan(), id);
    if (floor == nullptr || findStair(*floor, id) == nullptr) {
        return std::nullopt;
    }
    return failure(command, fail(ErrorCode::InvalidValue, id + " is a stair, not " + (kind == "opening" ? "an " : "a ") +
                                                              kind + ". Use stair remove."));
}

Json removedJson(const std::vector<Id>& items, const std::vector<Id>& detached, const std::vector<Id>& openings,
                 const std::vector<Id>& rooms, const std::vector<Id>& walls, const std::vector<Id>& points) {
    Json removed = Json::object();
    removed["items"] = items;
    removed["detached"] = detached;
    removed["openings"] = openings;
    removed["rooms"] = rooms;
    removed["walls"] = walls;
    removed["points"] = points;
    Json result = Json::object();
    result["removed"] = std::move(removed);
    return result;
}

}
