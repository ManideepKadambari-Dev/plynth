#include "plynth/core/BuildJob.h"

#include "plynth/core/Items.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"

#include <algorithm>
#include <map>
#include <set>

namespace plynth {

namespace {

// A problem found while reading a result.
struct Problem {
    std::string where;
    std::string message;
};

const Json& field(const Json& object, const char* key, const std::string& where)
{
    if (!object.is_object() || !object.contains(key)) throw Problem{where + "/" + key, std::string("Missing field \"") + key + "\"."};
    return object[key];
}

// A whole number of at least zero.
long long count(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_number_integer() || v.get<long long>() < 0) throw Problem{where + "/" + key, "Must be a whole number of at least 0."};
    return v.get<long long>();
}

// Text, or null for an empty text.
std::string optionalText(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_null() && !v.is_string()) throw Problem{where + "/" + key, "Must be text or null."};
    return v.is_string() ? v.get<std::string>() : std::string();
}

// A required text.
std::string text(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_string()) throw Problem{where + "/" + key, "Must be text."};
    return v.get<std::string>();
}

// Text, or null when empty.
Json textOrNull(const std::string& value)
{
    return value.empty() ? Json(nullptr) : Json(value);
}

// [x, y].
Json xy(Vec2 p)
{
    return Json::array({p.x, p.y});
}

// [x, y, z].
Json xyz(const Vec3& p)
{
    return Json::array({p.x, p.y, p.z});
}

// A list of [x, y].
Json xyList(const std::vector<Vec2>& points)
{
    Json list = Json::array();
    for (const Vec2& p : points) list.push_back(xy(p));
    return list;
}

// A list of [x, y, z].
Json xyzList(const std::vector<Vec3>& points)
{
    Json list = Json::array();
    for (const Vec3& p : points) list.push_back(xyz(p));
    return list;
}

// A wall solid ring.
Json ringJson(const WallRing& ring)
{
    Json edges = Json::array();
    for (const WallEdge& e : ring.edges) {
        Json edge = Json::object();
        edge["wall"] = e.wall;
        edge["side"] = e.side ? Json(std::string(wallSideName(*e.side))) : Json(nullptr);
        edge["room"] = textOrNull(e.room);
        edges.push_back(std::move(edge));
    }
    Json json = Json::object();
    json["points"] = xyList(ring.points);
    json["edges"] = std::move(edges);
    return json;
}

// One opening as the worker reads it.
Json openingJson(const OpeningBuild& o)
{
    Json json = Json::object();
    json["id"] = o.id;
    json["wall"] = o.wall;
    json["type"] = std::string(openingTypeName(o.type));
    json["shape"] = o.shape;
    json["width"] = o.width;
    json["height"] = o.height;
    json["sill"] = o.sill;
    json["hinge"] = std::string(hingeEndName(o.hinge));
    json["swing"] = std::string(wallSideName(o.swing));
    json["frame"] = o.frame ? Json(*o.frame) : Json(nullptr);
    json["centre"] = xy(o.centre);
    json["tangent"] = xy(o.tangent);
    json["right"] = xy(o.right);
    json["thickness"] = o.thickness;
    json["curved"] = o.curved;
    json["outline"] = xyList(o.outline);
    json["inner_outline"] = xyList(o.innerOutline);
    Json cutter = Json::object();
    cutter["right"] = xyzList(o.cutter.right);
    cutter["left"] = xyzList(o.cutter.left);
    json["cutter"] = std::move(cutter);
    json["threshold"] = xyList(o.threshold);
    return json;
}

// One room as the worker reads it.
Json roomJson(const RoomBuild& r)
{
    Json polygons = Json::array();
    for (const WorldPolygon& p : r.polygons) {
        Json holes = Json::array();
        for (const std::vector<Vec2>& h : p.holes) holes.push_back(xyList(h));
        Json polygon = Json::object();
        polygon["outline"] = xyList(p.outline);
        polygon["holes"] = std::move(holes);
        polygons.push_back(std::move(polygon));
    }
    Json json = Json::object();
    json["id"] = r.id;
    json["type"] = r.type ? Json(*r.type) : Json(nullptr);
    json["area"] = r.area;
    json["floor_z"] = r.floorZ;
    json["ceiling_z"] = r.ceilingZ;
    json["polygons"] = std::move(polygons);
    return json;
}

// One item as the worker reads it.
Json itemJson(const ItemBuild& i)
{
    Json size = Json::object();
    size["width"] = i.size.width;
    size["depth"] = i.size.depth;
    size["height"] = i.size.height;
    Json json = Json::object();
    json["id"] = i.id;
    json["asset"] = i.asset;
    json["location"] = xyz(i.location);
    json["rotation_z"] = i.rotationZ;
    json["size"] = std::move(size);
    json["wall"] = i.wall ? Json(*i.wall) : Json(nullptr);
    return json;
}

// One floor as the worker reads it.
Json floorJson(const FloorBuild& f)
{
    Json solids = Json::array();
    for (const WallSolid& s : f.wallSolids) {
        Json holes = Json::array();
        for (const WallRing& h : s.holes) holes.push_back(ringJson(h));
        Json solid = Json::object();
        solid["height"] = s.height;
        solid["outline"] = ringJson(s.outline);
        solid["holes"] = std::move(holes);
        solids.push_back(std::move(solid));
    }
    Json openings = Json::array();
    for (const OpeningBuild& o : f.openings) openings.push_back(openingJson(o));
    Json rooms = Json::array();
    for (const RoomBuild& r : f.rooms) rooms.push_back(roomJson(r));
    Json items = Json::array();
    for (const ItemBuild& i : f.items) items.push_back(itemJson(i));

    Json json = Json::object();
    json["id"] = f.id;
    json["elevation"] = f.elevation;
    json["ceiling_height"] = f.ceilingHeight;
    json["wall_solids"] = std::move(solids);
    json["openings"] = std::move(openings);
    json["rooms"] = std::move(rooms);
    json["items"] = std::move(items);
    return json;
}

// Makes a problem found before Blender starts.
BuildProblem checkProblem(std::string code, BuildSeverity severity, std::string message, const Id& floor, const Id& object)
{
    BuildProblem p;
    p.code = std::move(code);
    p.severity = severity;
    p.message = std::move(message);
    p.floor = floor;
    p.object = object;
    p.step = std::string(kCheckStep);
    return p;
}

// Reads one problem of a result.
BuildProblem readProblem(const Json& json, const std::string& where)
{
    BuildProblem p;
    p.code = text(json, "code", where);
    const std::string severity = text(json, "severity", where);
    const auto parsed = buildSeverityFromName(severity);
    if (!parsed) throw Problem{where + "/severity", "Must be \"error\" or \"warning\"."};
    p.severity = *parsed;
    p.message = text(json, "message", where);
    p.floor = optionalText(json, "floor", where);
    p.object = optionalText(json, "object", where);
    p.step = text(json, "step", where);
    return p;
}

}

// "error" or "warning".
std::string_view buildSeverityName(BuildSeverity severity)
{
    return severity == BuildSeverity::Error ? "error" : "warning";
}

// The severity for a name used in files.
std::optional<BuildSeverity> buildSeverityFromName(std::string_view name)
{
    if (name == "error") return BuildSeverity::Error;
    if (name == "warning") return BuildSeverity::Warning;
    return std::nullopt;
}

// Works out the build plan of a project.
BuildPlan makeBuildPlan(const Project& project, const BuildAssetSource& assets, const BuildMaterialSource&)
{
    BuildPlan plan;
    const std::vector<double> levels = floorElevations(project.plan);

    bool anyItems = false;
    for (const Floor& floor : project.plan.floors) anyItems = anyItems || !floor.items.empty();
    if (!assets && anyItems) {
        plan.problems.push_back(checkProblem("no_library", BuildSeverity::Error,
                                             "No library is set, so the furniture is left out of the build.", {}, {}));
    }

    std::map<std::string, BuildAsset> used;
    for (std::size_t index = 0; index < project.plan.floors.size(); ++index) {
        const Floor& floor = project.plan.floors[index];
        const double level = levels[index];
        FloorBuild build;
        build.id = floor.id;
        build.elevation = level;
        build.ceilingHeight = floor.ceilingHeight;
        build.wallSolids = wallSolids(floor);

        for (const Opening& opening : floor.openings) {
            OpeningBuildResult made = openingBuild(floor, opening, level);
            if (!made.status.ok()) {
                plan.problems.push_back(
                    checkProblem("opening_does_not_fit", BuildSeverity::Error, made.status.message, floor.id, opening.id));
                continue;
            }
            build.openings.push_back(std::move(made.opening));
        }

        for (const Room& room : floor.rooms) {
            std::optional<RoomBuild> made = roomBuild(floor, room, level);
            if (!made) {
                plan.problems.push_back(checkProblem("room_not_closed", BuildSeverity::Error,
                                                     "Room " + room.id + " is not closed by its walls, so it has no floor or ceiling.",
                                                     floor.id, room.id));
                continue;
            }
            build.rooms.push_back(std::move(*made));
        }

        std::map<Id, std::vector<ItemProblem>> itemWarnings;
        if (assets) {
            for (ItemProblem& problem : itemProblems(floor)) itemWarnings[problem.item].push_back(std::move(problem));
        }
        for (const Item& item : floor.items) {
            if (!assets) continue;
            const std::optional<BuildAssetState> state = assets(item.asset);
            if (!state) {
                plan.problems.push_back(checkProblem("missing_asset", BuildSeverity::Error,
                                                     "Item " + item.id + " uses the asset " + item.asset +
                                                         ", which is not in the library, so it is left out.",
                                                     floor.id, item.id));
                continue;
            }
            if (state->broken) {
                std::string message = "Item " + item.id + " uses the asset " + item.asset + ", which is broken";
                if (!state->problem.empty()) message += ": " + state->problem;
                else message += ".";
                message += " It is left out.";
                plan.problems.push_back(checkProblem("asset_broken", BuildSeverity::Error, message, floor.id, item.id));
                continue;
            }
            used.emplace(item.asset, BuildAsset{item.asset, state->blendFile});
            const ItemPlacement placement = itemPlacement(item, level);
            ItemBuild built;
            built.id = item.id;
            built.asset = item.asset;
            built.location = placement.location;
            built.rotationZ = placement.rotationZ;
            built.size = item.size;
            built.wall = item.wall;
            build.items.push_back(std::move(built));
            for (const ItemProblem& problem : itemWarnings[item.id]) {
                plan.problems.push_back(checkProblem(std::string(itemProblemName(problem.kind)), BuildSeverity::Warning,
                                                     problem.message, floor.id, item.id));
            }
        }
        plan.floors.push_back(std::move(build));
    }
    for (auto& [id, asset] : used) plan.assets.push_back(std::move(asset));
    return plan;
}

// A problem in its file form.
Json buildProblemToJson(const BuildProblem& problem)
{
    Json json = Json::object();
    json["code"] = problem.code;
    json["severity"] = std::string(buildSeverityName(problem.severity));
    json["message"] = problem.message;
    json["floor"] = textOrNull(problem.floor);
    json["object"] = textOrNull(problem.object);
    json["step"] = problem.step;
    return json;
}

// Says whether a plan has anything to build.
Status checkBuildable(const Plan& plan)
{
    for (const Floor& floor : plan.floors) {
        if (!floor.walls.empty()) return {};
    }
    return fail(ErrorCode::InvalidValue, "The plan has no walls to build.");
}

// Makes the build job.
Json makeBuildJob(const Project& project, const BuildPlan& plan, const BuildSettings& settings,
                  const std::filesystem::path& projectFolder, std::string_view jobId,
                  const std::filesystem::path& resultFile)
{
    Json params = Json::object();
    Json info = Json::object();
    info["name"] = project.settings.name;
    info["folder"] = pathToUtf8(projectFolder);
    params["project"] = std::move(info);
    params["output_folder"] = pathToUtf8(projectFolder / pathFromUtf8(kBuildFolderName));
    params["blend_file"] = std::string(kBuildBlendFileName);
    params["fbx_file"] = settings.writeFbx ? Json(std::string(kBuildFbxFileName)) : Json(nullptr);
    params["plan"] = planToJson(project.plan);
    Json set = Json::object();
    set["bevel_width"] = settings.bevelWidth;
    set["bevel_segments"] = settings.bevelSegments;
    set["fill_openings"] = settings.fillOpenings;
    params["settings"] = std::move(set);
    Json assetList = Json::array();
    for (const BuildAsset& a : plan.assets) {
        Json asset = Json::object();
        asset["id"] = a.id;
        asset["blend_file"] = pathToUtf8(a.blendFile);
        asset["collection"] = a.id;
        assetList.push_back(std::move(asset));
    }
    params["assets"] = std::move(assetList);
    Json floors = Json::array();
    for (const FloorBuild& f : plan.floors) floors.push_back(floorJson(f));
    params["floors"] = std::move(floors);
    return makeBlenderJob(kBuildTask, jobId, resultFile, std::move(params));
}

// Reads the result of a build job.
BuildWorkerOutput readBuildResult(const Json& result, std::string_view jobId)
{
    BuildWorkerOutput out;
    out.header = readBlenderResultHeader(result, kBuildTask, jobId);
    if (!out.header.status.ok()) return out;
    try {
        const Json& output = result["output"];
        out.blendFile = text(output, "blend_file", "/output");
        out.fbxFile = optionalText(output, "fbx_file", "/output");
        out.objects = count(output, "objects", "/output");
        out.triangles = count(output, "triangles", "/output");
        const Json& counts = field(output, "counts", "/output");
        if (!counts.is_object()) throw Problem{"/output/counts", "Must be an object."};
        out.counts.wallSolids = static_cast<int>(count(counts, "wall_solids", "/output/counts"));
        out.counts.openingsCut = static_cast<int>(count(counts, "openings_cut", "/output/counts"));
        out.counts.fills = static_cast<int>(count(counts, "fills", "/output/counts"));
        out.counts.rooms = static_cast<int>(count(counts, "rooms", "/output/counts"));
        out.counts.items = static_cast<int>(count(counts, "items", "/output/counts"));
        const Json& steps = field(output, "steps", "/output");
        if (!steps.is_array()) throw Problem{"/output/steps", "Must be a list."};
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const std::string where = "/output/steps/" + std::to_string(i);
            out.steps.push_back({text(steps[i], "step", where), count(steps[i], "ms", where)});
        }
        const Json& problems = field(output, "problems", "/output");
        if (!problems.is_array()) throw Problem{"/output/problems", "Must be a list."};
        for (std::size_t i = 0; i < problems.size(); ++i) {
            out.problems.push_back(readProblem(problems[i], "/output/problems/" + std::to_string(i)));
        }
    } catch (const Problem& p) {
        out.header.status = fail(ErrorCode::BadFile, "The Blender result is not valid: " + p.message, p.where);
    }
    return out;
}

}
