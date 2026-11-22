#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/MaterialDefaults.h>
#include <plynth/core/Materials.h>
#include <plynth/core/Plan.h>
#include <plynth/core/commands/MaterialCommands.h>
#include <plynth/core/commands/RoofCommands.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/commands/StairCommands.h>

#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct SetOptions {
    std::string project;
    std::string library;
    std::string target;
    std::string material;
};

struct ListOptions {
    std::string project;
    std::string library;
};

// A material id as JSON, null when there is none.
Json materialJson(const std::optional<std::string>& material) {
    if (material) {
        return *material;
    }
    return nullptr;
}

// A resolved material as {material, origin, from}.
Json resolvedJson(const ResolvedMaterial& resolved) {
    Json json = Json::object();
    json["material"] = materialJson(resolved.material);
    json["origin"] = std::string(materialOriginName(resolved.origin));
    if (resolved.from.empty()) {
        json["from"] = nullptr;
    } else {
        json["from"] = resolved.from;
    }
    return json;
}

// The material a target names itself, before anything is resolved: what "set" replaces.
std::optional<std::string> markedMaterial(const Project& project, const MaterialTarget& target) {
    switch (target.kind) {
    case MaterialTarget::Kind::Default:
        return materialDefault(project.settings.materials, target.surface);
    case MaterialTarget::Kind::Wall: {
        const Floor* floor = findFloorContaining(project.plan, target.object);
        const Wall* wall = floor ? findWall(*floor, target.object) : nullptr;
        if (wall == nullptr) {
            return std::nullopt;
        }
        return target.wallFace == WallFace::Inside ? wall->materialInside : wall->materialOutside;
    }
    case MaterialTarget::Kind::Room: {
        const Floor* floor = findFloorContaining(project.plan, target.object);
        const Room* room = floor ? findRoom(*floor, target.object) : nullptr;
        if (room == nullptr) {
            return std::nullopt;
        }
        switch (target.roomSurface) {
        case RoomSurface::Floor:
            return room->floorMaterial;
        case RoomSurface::Walls:
            return room->wallMaterial;
        case RoomSurface::Ceiling:
            return room->ceilingMaterial;
        }
        break;
    }
    case MaterialTarget::Kind::Stair: {
        for (const Floor& floor : project.plan.floors) {
            if (const Stair* stair = findStair(floor, target.object)) {
                return stair->material;
            }
        }
        return std::nullopt;
    }
    case MaterialTarget::Kind::Roof: {
        const RoofSection* entry = findRoofSection(project.plan, target.object);
        return entry ? entry->material : std::nullopt;
    }
    }
    return std::nullopt;
}

// The command that sets a target.
std::unique_ptr<Command> makeSetCommand(const Plan& plan, const MaterialTarget& target,
                                        std::optional<std::string> material) {
    switch (target.kind) {
    case MaterialTarget::Kind::Default:
        return std::make_unique<SetDefaultMaterialCommand>(target.surface, std::move(material));
    case MaterialTarget::Kind::Wall:
        return std::make_unique<SetWallMaterialCommand>(target.object, target.wallFace, std::move(material));
    case MaterialTarget::Kind::Stair:
        for (const Floor& floor : plan.floors) {
            if (const Stair* stair = findStair(floor, target.object)) {
                Stair values = *stair;
                values.material = std::move(material);
                return std::make_unique<SetStairCommand>(target.object, std::move(values));
            }
        }
        break;
    case MaterialTarget::Kind::Roof:
        if (const RoofSection* entry = findRoofSection(plan, target.object)) {
            return std::make_unique<SetRoofSectionCommand>(target.object, entry->settings, std::move(material));
        }
        break;
    case MaterialTarget::Kind::Room:
        break;
    }
    return std::make_unique<SetRoomMaterialCommand>(target.object, target.roomSurface, std::move(material));
}

// Sets or clears the material of one place. The target and the name are checked before the project changes.
Answer materialSet(const RunContext& context, const SetOptions& options) {
    const std::string name = "material set";
    return editProject(name, context, options.project, [&](Document& document) {
        const MaterialTargetParse parsed = parseMaterialTarget(document.plan(), options.target);
        if (!parsed.status.ok()) {
            return failure(name, parsed.status);
        }
        std::optional<std::string> material;
        if (options.material != "none") {
            material = options.material;
            Answer badId;
            if (!checkIdArgument(name, options.material, badId)) {
                return badId;
            }
            OpenedMaterialCatalog opened;
            Answer noLibrary;
            if (!openMaterialCatalog(name, context, options.library, opened, noLibrary)) {
                return noLibrary;
            }
            const MaterialForUse usable = materialForUse(*opened.catalog, options.material);
            if (!usable.status.ok()) {
                return failure(name, usable.status);
            }
        }
        const std::optional<std::string> previous = markedMaterial(document.project(), parsed.target);
        Answer failed;
        if (!runCommand(name, document, makeSetCommand(document.plan(), parsed.target, material), failed)) {
            return failed;
        }
        Json result = Json::object();
        result["target"] = options.target;
        result["material"] = materialJson(material);
        result["previous"] = materialJson(previous);
        result["resolved"] = resolvedJson(materialAt(document.project(), parsed.target));
        return success(name, std::move(result));
    });
}

// The project defaults as {surface: id or null}.
Json defaultsJson(const MaterialDefaults& defaults) {
    Json json = Json::object();
    for (const Surface surface : allSurfaces()) {
        json[std::string(surfaceName(surface))] = materialJson(materialDefault(defaults, surface));
    }
    return json;
}

// The rooms of a floor that list a wall.
std::vector<const Room*> roomsOfWall(const Floor& floor, const Wall& wall) {
    std::vector<const Room*> rooms;
    for (const Room& room : floor.rooms) {
        for (const Id& id : room.walls) {
            if (id == wall.id) {
                rooms.push_back(&room);
                break;
            }
        }
    }
    return rooms;
}

// Shows the defaults, the material of every wall face and room surface, and every material in use.
Answer materialList(const RunContext& context, const ListOptions& options) {
    const std::string name = "material list";
    Document document;
    Answer failed;
    if (!openProject(name, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Project& project = document.project();
    OpenedMaterialCatalog opened;
    bool haveLibrary = false;
    if (!openMaterialCatalogIfAny(name, context, options.library, opened, haveLibrary, failed)) {
        return failed;
    }
    Json walls = Json::array();
    Json rooms = Json::array();
    for (const Floor& floor : project.plan.floors) {
        for (const Wall& wall : floor.walls) {
            Json inside = Json::array();
            for (const Room* room : roomsOfWall(floor, wall)) {
                Json entry = resolvedJson(wallFaceMaterial(project, wall, room));
                entry["room"] = room->id;
                inside.push_back(std::move(entry));
            }
            Json entry = Json::object();
            entry["id"] = wall.id;
            entry["floor"] = floor.id;
            entry["inside"] = std::move(inside);
            entry["outside"] = resolvedJson(wallFaceMaterial(project, wall, nullptr));
            walls.push_back(std::move(entry));
        }
        for (const Room& room : floor.rooms) {
            Json materials = Json::object();
            materials["floor"] = resolvedJson(roomFloorMaterial(project, room));
            materials["wall"] = resolvedJson(roomWallMaterial(project, room));
            materials["ceiling"] = resolvedJson(roomCeilingMaterial(project, room));
            Json entry = Json::object();
            entry["id"] = room.id;
            entry["floor"] = floor.id;
            entry["type"] = room.type ? Json(*room.type) : Json(nullptr);
            entry["materials"] = std::move(materials);
            rooms.push_back(std::move(entry));
        }
    }
    Json stairs = Json::array();
    for (const Floor& floor : project.plan.floors) {
        for (const Stair& stair : floor.stairs) {
            Json entry = Json::object();
            entry["id"] = stair.id;
            entry["floor"] = floor.id;
            entry["material"] = resolvedJson(stairMaterial(project, stair));
            stairs.push_back(std::move(entry));
        }
    }
    Json roofs = Json::array();
    for (const RoofArea& area : roofAreas(project.plan)) {
        Json entry = Json::object();
        entry["floor"] = area.floor;
        entry["index"] = area.index;
        entry["section"] = area.section.empty() ? Json(nullptr) : Json(area.section);
        entry["material"] = resolvedJson(roofPartMaterial(project, area, RoofPart::Covering));
        roofs.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["defaults"] = defaultsJson(project.settings.materials);
    result["walls"] = std::move(walls);
    result["rooms"] = std::move(rooms);
    result["stairs"] = std::move(stairs);
    result["roofs"] = std::move(roofs);
    const std::vector<std::string> used = materialsUsed(project);
    Json missing = Json::array();
    if (haveLibrary) {
        for (const std::string& id : used) {
            if (materialForUse(*opened.catalog, id).status.code == ErrorCode::NotFound) {
                missing.push_back(id);
            }
        }
        result["library"] = pathToUtf8(opened.paths.root);
    } else {
        result["library"] = nullptr;
    }
    result["used"] = used;
    result["missing"] = std::move(missing);
    return success(name, std::move(result));
}

}

std::vector<CommandSpec> materialCommands() {
    CommandSpec set;
    set.words = {"material", "set"};
    set.description = "Set or clear the material of a wall face, a room surface or a project default. "
                      "Targets: w3.inside, w3.outside, r1.floor, r1.wall, r1.ceiling, s1, rf1, hall.floor, "
                      "default.wall_outside.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("target", options->target, "Where to set it, for example r1.floor.")->required();
        sub.add_option("material", options->material, "A material id, or none to clear it.")->required();
        return [options](const RunContext& context) { return materialSet(context, *options); };
    };

    CommandSpec list;
    list.words = {"material", "list"};
    list.description = "List the default materials, the material of every wall face, room surface, stair and "
                       "roof section, and the materials in use.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        return [options](const RunContext& context) { return materialList(context, *options); };
    };

    return {set, list};
}

}
