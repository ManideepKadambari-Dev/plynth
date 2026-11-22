#include "plynth/core/ProjectJson.h"

#include "plynth/core/MaterialDefaults.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace plynth {

namespace {

// Thrown inside this file when the JSON breaks a rule. Caught at the reader boundary.
struct ReadError {
    Status status;
};

// Stops reading with a BadFile error at a JSON pointer.
[[noreturn]] void refuse(const std::string& message, const std::string& where)
{
    throw ReadError{fail(ErrorCode::BadFile, message, where)};
}

// The JSON pointer of a key inside an object at a pointer.
std::string child(const std::string& where, const std::string& key)
{
    return where + "/" + key;
}

// Requires an object and refuses keys that are not in the allowed list.
const Json& expectObject(const Json& j, const std::string& where, std::initializer_list<const char*> allowed)
{
    if (!j.is_object()) refuse("This value must be an object.", where);
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool known = false;
        for (const char* key : allowed) known = known || it.key() == key;
        if (!known) refuse("The field \"" + it.key() + "\" is not part of the format.", child(where, it.key()));
    }
    return j;
}

// Reads a required string.
std::string getString(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end()) refuse(std::string("The field \"") + key + "\" is missing.", child(where, key));
    if (!it->is_string()) refuse("This value must be text.", child(where, key));
    return it->get<std::string>();
}

// Reads a string that may be missing or null.
std::optional<std::string> getOptionalString(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (!it->is_string()) refuse("This value must be text or null.", child(where, key));
    return it->get<std::string>();
}

// Reads a number that is present. null is refused because a non finite number is written as null.
double numberAt(const Json& value, const std::string& where)
{
    if (!value.is_number()) refuse("This value must be a number.", where);
    const double d = value.get<double>();
    if (!std::isfinite(d)) refuse("This number is not finite.", where);
    return d;
}

// Reads a required number.
double getNumber(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end()) refuse(std::string("The field \"") + key + "\" is missing.", child(where, key));
    return numberAt(*it, child(where, key));
}

// Reads a number that may be missing, which gives the default.
double getNumberOr(const Json& obj, const char* key, const std::string& where, double fallback)
{
    auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    return numberAt(*it, child(where, key));
}

// Reads a number that may be missing or null.
std::optional<double> getOptionalNumber(const Json& obj, const char* key, const std::string& where)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    return numberAt(*it, child(where, key));
}

// Reads an array, which must be present when required.
const Json* getArray(const Json& obj, const char* key, const std::string& where, bool required)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (required) refuse(std::string("The field \"") + key + "\" is missing.", child(where, key));
        return nullptr;
    }
    if (!it->is_array()) refuse("This value must be a list.", child(where, key));
    return &*it;
}

// Reads one point.
Point readPoint(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "x", "y", "corner_radius"});
    Point p;
    p.id = getString(j, "id", where);
    p.x = getNumber(j, "x", where);
    p.y = getNumber(j, "y", where);
    p.cornerRadius = getNumberOr(j, "corner_radius", where, 0.0);
    return p;
}

// Reads one wall.
Wall readWall(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "from", "to", "thickness", "height", "bulge", "material_inside", "material_outside"});
    Wall w;
    w.id = getString(j, "id", where);
    w.from = getString(j, "from", where);
    w.to = getString(j, "to", where);
    w.thickness = getNumberOr(j, "thickness", where, kDefaultWallThickness);
    w.height = getOptionalNumber(j, "height", where);
    w.bulge = getNumberOr(j, "bulge", where, 0.0);
    w.materialInside = getOptionalString(j, "material_inside", where);
    w.materialOutside = getOptionalString(j, "material_outside", where);
    return w;
}

// Reads one room.
Room readRoom(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "type", "walls", "floor_material", "wall_material", "ceiling_material"});
    Room r;
    r.id = getString(j, "id", where);
    r.type = getOptionalString(j, "type", where);
    const Json* walls = getArray(j, "walls", where, true);
    for (std::size_t i = 0; i < walls->size(); ++i) {
        const std::string at = child(child(where, "walls"), std::to_string(i));
        if (!(*walls)[i].is_string()) refuse("This value must be text.", at);
        r.walls.push_back((*walls)[i].get<std::string>());
    }
    r.floorMaterial = getOptionalString(j, "floor_material", where);
    r.wallMaterial = getOptionalString(j, "wall_material", where);
    r.ceilingMaterial = getOptionalString(j, "ceiling_material", where);
    return r;
}

// Reads one opening.
Opening readOpening(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "wall", "offset", "type", "shape", "width", "height", "sill", "hinge", "swing", "frame"});
    Opening o;
    o.id = getString(j, "id", where);
    o.wall = getString(j, "wall", where);
    o.offset = getNumber(j, "offset", where);
    const auto type = openingTypeFromName(getString(j, "type", where));
    if (!type) refuse("The opening type must be \"door\", \"window\" or \"plain\".", child(where, "type"));
    o.type = *type;
    if (j.contains("shape")) o.shape = getString(j, "shape", where);
    o.width = getNumber(j, "width", where);
    o.height = getNumber(j, "height", where);
    o.sill = getNumberOr(j, "sill", where, 0.0);
    if (j.contains("hinge")) {
        const auto hinge = hingeEndFromName(getString(j, "hinge", where));
        if (!hinge) refuse("The hinge must be \"start\" or \"end\".", child(where, "hinge"));
        o.hinge = *hinge;
    }
    if (j.contains("swing")) {
        const auto swing = wallSideFromName(getString(j, "swing", where));
        if (!swing) refuse("The swing must be \"left\" or \"right\".", child(where, "swing"));
        o.swing = *swing;
    }
    o.frame = getOptionalString(j, "frame", where);
    return o;
}

// Reads one placed item.
Item readItem(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "asset", "room", "x", "y", "z", "rotation", "wall", "size"});
    Item it;
    it.id = getString(j, "id", where);
    it.asset = getString(j, "asset", where);
    it.room = getOptionalString(j, "room", where);
    it.x = getNumber(j, "x", where);
    it.y = getNumber(j, "y", where);
    it.z = getNumberOr(j, "z", where, 0.0);
    it.rotation = getNumberOr(j, "rotation", where, 0.0);
    it.wall = getOptionalString(j, "wall", where);
    auto sit = j.find("size");
    if (sit == j.end()) refuse("The field \"size\" is missing.", child(where, "size"));
    const std::string at = child(where, "size");
    expectObject(*sit, at, {"width", "depth", "height"});
    it.size.width = getNumber(*sit, "width", at);
    it.size.depth = getNumber(*sit, "depth", at);
    it.size.height = getNumber(*sit, "height", at);
    return it;
}

// Reads every item of an optional array with a reader function.
template <typename Item, typename Reader>
std::vector<Item> readList(const Json& obj, const char* key, const std::string& where, Reader reader)
{
    std::vector<Item> items;
    const Json* array = getArray(obj, key, where, false);
    if (!array) return items;
    for (std::size_t i = 0; i < array->size(); ++i) {
        items.push_back(reader((*array)[i], child(child(where, key), std::to_string(i))));
    }
    return items;
}

// Reads one floor.
Floor readFloor(const Json& j, const std::string& where)
{
    expectObject(j, where, {"id", "name", "image", "ceiling_height", "points", "walls", "rooms", "openings", "items"});
    Floor f;
    f.id = getString(j, "id", where);
    f.name = j.contains("name") ? getString(j, "name", where) : f.id;
    f.image = getOptionalString(j, "image", where);
    f.ceilingHeight = getNumberOr(j, "ceiling_height", where, kDefaultCeilingHeight);
    f.points = readList<Point>(j, "points", where, readPoint);
    f.walls = readList<Wall>(j, "walls", where, readWall);
    f.rooms = readList<Room>(j, "rooms", where, readRoom);
    f.openings = readList<Opening>(j, "openings", where, readOpening);
    f.items = readList<Item>(j, "items", where, readItem);
    return f;
}

// Checks "format" and upgrades the JSON to the current version. found gets the version in the file, even when the upgrade fails.
void prepare(Json& json, std::string_view format, int currentVersion, std::span<const Migration> steps, int& found,
             bool& migrated)
{
    if (!json.is_object()) refuse("The file must hold one JSON object.", "");
    auto fit = json.find("format");
    if (fit == json.end()) refuse("The field \"format\" is missing.", "/format");
    if (!fit->is_string() || fit->get<std::string>() != format) {
        refuse("This is not a " + std::string(format) + " file.", "/format");
    }
    found = 0;
    auto vit = json.find("version");
    if (vit != json.end() && vit->is_number_integer()) found = vit->get<int>();

    if (auto s = migrateJson(json, currentVersion, steps); !s.ok()) throw ReadError{s};
    migrated = found != currentVersion;
}

}

// Turns a plan into its plan.json form.
Json planToJson(const Plan& plan)
{
    Json out = Json::object();
    out["format"] = std::string(kPlanFormatName);
    out["version"] = kPlanFormatVersion;

    Json scale = Json::object();
    scale["pixels_per_metre"] = plan.scale.pixelsPerMetre ? Json(*plan.scale.pixelsPerMetre) : Json(nullptr);
    out["scale"] = std::move(scale);

    const auto optString = [](const std::optional<std::string>& v) { return v ? Json(*v) : Json(nullptr); };
    const auto optNumber = [](const std::optional<double>& v) { return v ? Json(*v) : Json(nullptr); };

    Json floors = Json::array();
    for (const Floor& f : plan.floors) {
        Json jf = Json::object();
        jf["id"] = f.id;
        jf["name"] = f.name;
        jf["image"] = optString(f.image);
        jf["ceiling_height"] = f.ceilingHeight;

        Json points = Json::array();
        for (const Point& p : f.points) {
            Json jp = Json::object();
            jp["id"] = p.id;
            jp["x"] = p.x;
            jp["y"] = p.y;
            jp["corner_radius"] = p.cornerRadius;
            points.push_back(std::move(jp));
        }
        jf["points"] = std::move(points);

        Json walls = Json::array();
        for (const Wall& w : f.walls) {
            Json jw = Json::object();
            jw["id"] = w.id;
            jw["from"] = w.from;
            jw["to"] = w.to;
            jw["thickness"] = w.thickness;
            jw["height"] = optNumber(w.height);
            jw["bulge"] = w.bulge;
            jw["material_inside"] = optString(w.materialInside);
            jw["material_outside"] = optString(w.materialOutside);
            walls.push_back(std::move(jw));
        }
        jf["walls"] = std::move(walls);

        Json rooms = Json::array();
        for (const Room& r : f.rooms) {
            Json jr = Json::object();
            jr["id"] = r.id;
            jr["type"] = optString(r.type);
            Json rw = Json::array();
            for (const Id& id : r.walls) rw.push_back(id);
            jr["walls"] = std::move(rw);
            jr["floor_material"] = optString(r.floorMaterial);
            jr["wall_material"] = optString(r.wallMaterial);
            jr["ceiling_material"] = optString(r.ceilingMaterial);
            rooms.push_back(std::move(jr));
        }
        jf["rooms"] = std::move(rooms);

        Json openings = Json::array();
        for (const Opening& o : f.openings) {
            Json jo = Json::object();
            jo["id"] = o.id;
            jo["wall"] = o.wall;
            jo["offset"] = o.offset;
            jo["type"] = std::string(openingTypeName(o.type));
            jo["shape"] = o.shape;
            jo["width"] = o.width;
            jo["height"] = o.height;
            jo["sill"] = o.sill;
            jo["hinge"] = std::string(hingeEndName(o.hinge));
            jo["swing"] = std::string(wallSideName(o.swing));
            jo["frame"] = optString(o.frame);
            openings.push_back(std::move(jo));
        }
        jf["openings"] = std::move(openings);

        Json items = Json::array();
        for (const Item& it : f.items) {
            Json ji = Json::object();
            ji["id"] = it.id;
            ji["asset"] = it.asset;
            ji["room"] = optString(it.room);
            ji["x"] = it.x;
            ji["y"] = it.y;
            ji["z"] = it.z;
            ji["rotation"] = it.rotation;
            ji["wall"] = optString(it.wall);
            Json size = Json::object();
            size["width"] = it.size.width;
            size["depth"] = it.size.depth;
            size["height"] = it.size.height;
            ji["size"] = std::move(size);
            items.push_back(std::move(ji));
        }
        jf["items"] = std::move(items);
        floors.push_back(std::move(jf));
    }
    out["floors"] = std::move(floors);
    return out;
}

// Reads a plan from its plan.json form.
PlanRead planFromJson(const Json& json)
{
    PlanRead result;
    try {
        Json copy = json;
        prepare(copy, kPlanFormatName, kPlanFormatVersion, planMigrations(), result.fileVersion, result.migrated);
        expectObject(copy, "", {"format", "version", "scale", "floors"});

        Plan plan;
        auto sit = copy.find("scale");
        if (sit != copy.end()) {
            expectObject(*sit, "/scale", {"pixels_per_metre"});
            plan.scale.pixelsPerMetre = getOptionalNumber(*sit, "pixels_per_metre", "/scale");
        }
        const Json* floors = getArray(copy, "floors", "", true);
        for (std::size_t i = 0; i < floors->size(); ++i) {
            plan.floors.push_back(readFloor((*floors)[i], "/floors/" + std::to_string(i)));
        }
        if (Status s = checkPlan(plan); !s.ok()) {
            refuse(s.message, s.where);
        }
        result.plan = std::move(plan);
    } catch (const ReadError& e) {
        result.status = e.status;
        result.plan = Plan{};
    } catch (const std::exception& e) {
        result.status = fail(ErrorCode::BadFile, std::string("The plan could not be read: ") + e.what());
        result.plan = Plan{};
    }
    return result;
}

namespace {

// Turns default sizes into their project.json form.
Json defaultsToJson(const DefaultSizes& d)
{
    Json out = Json::object();
    out["ceiling_height"] = d.ceilingHeight;
    out["door_width"] = d.doorWidth;
    out["door_height"] = d.doorHeight;
    out["window_width"] = d.windowWidth;
    out["window_height"] = d.windowHeight;
    out["window_sill"] = d.windowSill;
    out["wall_item_centre"] = d.wallItemCentre;
    return out;
}


// Turns the default materials into their project.json form, every surface written.
Json materialsToJson(const MaterialDefaults& defaults)
{
    Json out = Json::object();
    for (const Surface surface : allSurfaces()) {
        const std::optional<std::string>& value = materialDefault(defaults, surface);
        out[std::string(surfaceName(surface))] = value ? Json(*value) : Json(nullptr);
    }
    return out;
}

}

// Turns project settings into their project.json form.
Json settingsToJson(const ProjectSettings& settings)
{
    Json out = Json::object();
    out["format"] = std::string(kProjectFormatName);
    out["version"] = kProjectFormatVersion;
    out["name"] = settings.name;
    out["export_mode"] = std::string(exportModeName(settings.exportMode));
    out["grid_size"] = settings.gridSize;
    out["defaults"] = defaultsToJson(settings.defaults);
    out["materials"] = materialsToJson(settings.materials);
    return out;
}

// Reads project settings from their project.json form.
SettingsRead settingsFromJson(const Json& json)
{
    SettingsRead result;
    try {
        Json copy = json;
        prepare(copy, kProjectFormatName, kProjectFormatVersion, projectMigrations(), result.fileVersion, result.migrated);
        expectObject(copy, "", {"format", "version", "name", "export_mode", "grid_size", "defaults", "materials"});

        ProjectSettings settings;
        settings.name = getString(copy, "name", "");
        if (settings.name.empty()) refuse("The project name is empty.", "/name");
        if (copy.contains("export_mode")) {
            const auto mode = exportModeFromName(getString(copy, "export_mode", ""));
            if (!mode) refuse("The export mode must be \"single\" or \"grouped\".", "/export_mode");
            settings.exportMode = *mode;
        }
        settings.gridSize = getNumberOr(copy, "grid_size", "", kDefaultGridSize);
        if (!(settings.gridSize > 0.0)) refuse("The grid size must be a number above zero.", "/grid_size");
        if (auto dit = copy.find("defaults"); dit != copy.end()) {
            expectObject(*dit, "/defaults",
                         {"ceiling_height", "door_width", "door_height", "window_width", "window_height", "window_sill", "wall_item_centre"});
            DefaultSizes& d = settings.defaults;
            d.ceilingHeight = getNumberOr(*dit, "ceiling_height", "/defaults", d.ceilingHeight);
            d.doorWidth = getNumberOr(*dit, "door_width", "/defaults", d.doorWidth);
            d.doorHeight = getNumberOr(*dit, "door_height", "/defaults", d.doorHeight);
            d.windowWidth = getNumberOr(*dit, "window_width", "/defaults", d.windowWidth);
            d.windowHeight = getNumberOr(*dit, "window_height", "/defaults", d.windowHeight);
            d.windowSill = getNumberOr(*dit, "window_sill", "/defaults", d.windowSill);
            d.wallItemCentre = getNumberOr(*dit, "wall_item_centre", "/defaults", d.wallItemCentre);
            if (Status s = checkDefaultSizes(d); !s.ok()) refuse(s.message, "/defaults");
        }
        if (auto mit = copy.find("materials"); mit != copy.end()) {
            expectObject(*mit, "/materials", {"wall_inside", "wall_outside", "wall_top", "floor", "ceiling", "door", "frame", "glass", "handle"});
            for (const Surface surface : allSurfaces()) {
                const std::string key(surfaceName(surface));
                if (!mit->contains(key)) continue;
                setMaterialDefault(settings.materials, surface, getOptionalString(*mit, key.c_str(), "/materials"));
            }
            if (Status s = checkMaterialDefaults(settings.materials); !s.ok()) refuse(s.message, s.where);
        }
        result.settings = std::move(settings);
    } catch (const ReadError& e) {
        result.status = e.status;
    } catch (const std::exception& e) {
        result.status = fail(ErrorCode::BadFile, std::string("The project settings could not be read: ") + e.what());
    }
    return result;
}

// Runs upgrade steps until the JSON reaches the current version.
Status migrateJson(Json& json, int currentVersion, std::span<const Migration> steps)
{
    if (!json.is_object()) return fail(ErrorCode::BadFile, "The file must hold one JSON object.");
    auto it = json.find("version");
    if (it == json.end()) return fail(ErrorCode::BadFile, "The field \"version\" is missing.", "/version");
    if (!it->is_number_integer()) return fail(ErrorCode::BadFile, "The version must be a whole number.", "/version");
    long long version = it->get<long long>();
    if (version < 1) return fail(ErrorCode::BadFile, "The version must be 1 or more.", "/version");
    if (version > currentVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    "This file is version " + std::to_string(version) + ", but this Plynth reads up to version "
                        + std::to_string(currentVersion) + ". Update Plynth to open it.",
                    "/version");
    }
    while (version < currentVersion) {
        const Migration* found = nullptr;
        for (const Migration& m : steps) {
            if (m.fromVersion == version) {
                found = &m;
                break;
            }
        }
        if (!found || !found->step) {
            return fail(ErrorCode::BadFile, "There is no upgrade step from version " + std::to_string(version) + ".", "/version");
        }
        if (Status s = found->step(json); !s.ok()) return s;
        ++version;
        json["version"] = version;
    }
    return {};
}

namespace {

// Version 1 to 2: every opening gets a hinge and a swing.
Status migratePlan1To2(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (!floor.is_object()) continue;
        auto openings = floor.find("openings");
        if (openings == floor.end() || !openings->is_array()) continue;
        for (Json& opening : *openings) {
            if (!opening.is_object()) continue;
            if (!opening.contains("hinge")) opening["hinge"] = "start";
            if (!opening.contains("swing")) opening["swing"] = "right";
        }
    }
    return {};
}

// Version 1 to 2: the project gets the built-in default sizes.
Status migrateProject1To2(Json& json)
{
    if (!json.is_object()) return {};
    if (!json.contains("defaults")) json["defaults"] = defaultsToJson(DefaultSizes{});
    return {};
}

// Version 2 to 3: every floor gets an empty list of items.
Status migratePlan2To3(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (floor.is_object() && !floor.contains("items")) floor["items"] = Json::array();
    }
    return {};
}

// Version 2 to 3: the defaults get the wall item centre.
Status migrateProject2To3(Json& json)
{
    if (!json.is_object()) return {};
    auto defaults = json.find("defaults");
    if (defaults == json.end() || !defaults->is_object()) return {};
    if (!defaults->contains("wall_item_centre")) (*defaults)["wall_item_centre"] = kDefaultWallItemCentre;
    return {};
}


// Version 3 to 4: every room gets an empty wall material.
Status migratePlan3To4(Json& json)
{
    auto floors = json.find("floors");
    if (floors == json.end() || !floors->is_array()) return {};
    for (Json& floor : *floors) {
        if (!floor.is_object()) continue;
        auto rooms = floor.find("rooms");
        if (rooms == floor.end() || !rooms->is_array()) continue;
        for (Json& room : *rooms) {
            if (room.is_object() && !room.contains("wall_material")) room["wall_material"] = nullptr;
        }
    }
    return {};
}

// Version 3 to 4: the project gets the built-in default materials.
Status migrateProject3To4(Json& json)
{
    if (!json.is_object()) return {};
    if (!json.contains("materials")) json["materials"] = materialsToJson(MaterialDefaults{});
    return {};
}

}

// The upgrade steps for plan.json.
std::span<const Migration> planMigrations()
{
    static const std::vector<Migration> steps = {{1, migratePlan1To2}, {2, migratePlan2To3}, {3, migratePlan3To4}};
    return steps;
}

// The upgrade steps for project.json.
std::span<const Migration> projectMigrations()
{
    static const std::vector<Migration> steps = {{1, migrateProject1To2}, {2, migrateProject2To3}, {3, migrateProject3To4}};
    return steps;
}

}
