#include "plynth/core/BuildJob.h"

#include "plynth/core/Items.h"
#include "plynth/core/Materials.h"
#include "plynth/core/Materials.h"
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

// An absolute path in the file form: UTF-8 with forward slashes.
std::string absolutePath(const std::filesystem::path& path)
{
    std::error_code ec;
    const std::filesystem::path full = std::filesystem::absolute(path, ec);
    return pathToUtf8(ec ? path : full);
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

// The material of a ring edge: the wall's face material for a side face, the wall top material for an end face.
std::string edgeMaterial(const WallEdge& edge, const FloorMaterials* materials)
{
    if (!materials) return {};
    if (!edge.side) return materials->wallTop;
    for (const WallFaceMaterial& face : materials->wallFaces) {
        if (face.wall == edge.wall && face.room == edge.room) return face.material;
    }
    return {};
}

// A wall solid ring.
Json ringJson(const WallRing& ring, const FloorMaterials* materials)
{
    Json edges = Json::array();
    for (const WallEdge& e : ring.edges) {
        Json edge = Json::object();
        edge["wall"] = e.wall;
        edge["side"] = e.side ? Json(std::string(wallSideName(*e.side))) : Json(nullptr);
        edge["room"] = textOrNull(e.room);
        edge["material"] = textOrNull(edgeMaterial(e, materials));
        edges.push_back(std::move(edge));
    }
    Json json = Json::object();
    json["points"] = xyList(ring.points);
    json["edges"] = std::move(edges);
    return json;
}

// One opening as the worker reads it.
Json openingJson(const OpeningBuild& o, const OpeningMaterialIds& ids)
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
    Json materials = Json::object();
    materials["reveal"] = textOrNull(ids.reveal);
    materials["threshold"] = textOrNull(ids.threshold);
    materials["door"] = textOrNull(ids.door);
    materials["frame"] = textOrNull(ids.frame);
    materials["glass"] = textOrNull(ids.glass);
    materials["handle"] = textOrNull(ids.handle);
    json["materials"] = std::move(materials);
    return json;
}

// One room as the worker reads it.
Json roomJson(const RoomBuild& r, const RoomMaterialIds& ids)
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
    json["floor_material"] = textOrNull(ids.floor);
    json["ceiling_material"] = textOrNull(ids.ceiling);
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
Json floorJson(const FloorBuild& f, const FloorMaterials* materials)
{
    Json solids = Json::array();
    for (const WallSolid& s : f.wallSolids) {
        Json holes = Json::array();
        for (const WallRing& h : s.holes) holes.push_back(ringJson(h, materials));
        Json solid = Json::object();
        solid["height"] = s.height;
        solid["top_material"] = textOrNull(materials ? materials->wallTop : std::string());
        solid["outline"] = ringJson(s.outline, materials);
        solid["holes"] = std::move(holes);
        solids.push_back(std::move(solid));
    }
    Json openings = Json::array();
    for (std::size_t i = 0; i < f.openings.size(); ++i) {
        const bool known = materials && i < materials->openings.size();
        openings.push_back(openingJson(f.openings[i], known ? materials->openings[i] : OpeningMaterialIds{}));
    }
    Json rooms = Json::array();
    for (std::size_t i = 0; i < f.rooms.size(); ++i) {
        const bool known = materials && i < materials->rooms.size();
        rooms.push_back(roomJson(f.rooms[i], known ? materials->rooms[i] : RoomMaterialIds{}));
    }
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

// Checks the material of each built surface against the library, keeps the usable ones and the problems.
class MaterialChecker {
public:
    explicit MaterialChecker(const BuildMaterialSource& source) : source_(source) {}

    // The id to put in the job for a resolved material, or empty for the neutral look.
    std::string use(const ResolvedMaterial& resolved, const Id& floor)
    {
        if (!resolved.material) return {};
        const std::string& id = *resolved.material;
        planUsesMaterial_ = true;
        if (!source_) return {};

        auto found = cache_.find(id);
        if (found == cache_.end()) found = cache_.emplace(id, source_(id)).first;
        const std::optional<BuildMaterialState>& state = found->second;
        if (state && !state->broken) {
            BuildMaterial material = state->material;
            material.id = id;
            usable_.emplace(id, std::move(material));
            return id;
        }
        const auto key = std::make_pair(id, resolved.from);
        if (problems_.count(key) != 0) return {};
        BuildProblem p;
        p.severity = BuildSeverity::Warning;
        p.step = std::string(kCheckStep);
        p.object = resolved.from;
        p.floor = resolved.from.empty() ? Id() : floor;
        std::string who;
        if (resolved.origin == MaterialOrigin::Wall) who = "Wall " + resolved.from;
        else if (resolved.origin == MaterialOrigin::Room) who = "Room " + resolved.from;
        else who = "The project default for " + std::string(surfaceName(resolved.surface));
        if (!state) {
            p.code = "missing_material";
            p.message = who + " uses the material " + id + ", which is not in the library, so it gets a plain look.";
        } else {
            p.code = "material_broken";
            p.message = who + " uses the material " + id + ", which is broken";
            p.message += state->problem.empty() ? "." : ": " + state->problem;
            p.message += " It gets a plain look.";
        }
        problems_.emplace(key, std::move(p));
        return {};
    }

    // Puts the usable materials and the material problems into the plan.
    void finish(BuildPlan& plan, bool alreadyNoLibrary)
    {
        for (auto& [id, material] : usable_) plan.materials.push_back(std::move(material));
        for (auto& [key, problem] : problems_) plan.problems.push_back(std::move(problem));
        if (!source_ && planUsesMaterial_ && !alreadyNoLibrary) {
            plan.problems.push_back(checkProblem("no_library", BuildSeverity::Warning,
                                                 "No library is set, so every surface gets a plain look.", {}, {}));
        }
    }

private:
    const BuildMaterialSource& source_;
    std::map<std::string, std::optional<BuildMaterialState>> cache_;
    std::map<std::string, BuildMaterial> usable_;
    std::map<std::pair<std::string, Id>, BuildProblem> problems_;
    bool planUsesMaterial_ = false;
};

// The materials of every built surface of one floor.
FloorMaterials floorMaterials(const Project& project, const Floor& floor, const FloorBuild& build, MaterialChecker& checker)
{
    FloorMaterials out;
    out.floor = floor.id;
    out.wallTop = checker.use(surfaceDefaultMaterial(project, Surface::WallTop), floor.id);

    std::set<std::pair<Id, Id>> faces;
    for (const WallSolid& solid : build.wallSolids) {
        const auto collect = [&faces](const WallRing& ring) {
            for (const WallEdge& e : ring.edges) {
                if (e.side) faces.emplace(e.wall, e.room);
            }
        };
        collect(solid.outline);
        for (const WallRing& hole : solid.holes) collect(hole);
    }
    for (const auto& [wallId, roomId] : faces) {
        const Wall* wall = findWall(floor, wallId);
        if (!wall) continue;
        const Room* room = roomId.empty() ? nullptr : findRoom(floor, roomId);
        out.wallFaces.push_back({wallId, roomId, checker.use(wallFaceMaterial(project, *wall, room), floor.id)});
    }

    for (const OpeningBuild& built : build.openings) {
        const Opening* opening = findOpening(floor, built.id);
        OpeningMaterialIds ids;
        ids.opening = built.id;
        if (opening) {
            const OpeningMaterials m = openingMaterials(project, floor, *opening);
            ids.reveal = checker.use(m.reveal, floor.id);
            if (built.sill == 0.0) ids.threshold = checker.use(m.threshold, floor.id);
            if (built.type == OpeningType::Door) {
                ids.door = checker.use(m.door, floor.id);
                ids.handle = checker.use(m.handle, floor.id);
            }
            if (built.type == OpeningType::Window) ids.glass = checker.use(m.glass, floor.id);
            if (built.type != OpeningType::Plain) ids.frame = checker.use(m.frame, floor.id);
        }
        out.openings.push_back(std::move(ids));
    }

    for (const RoomBuild& built : build.rooms) {
        const Room* room = findRoom(floor, built.id);
        RoomMaterialIds ids;
        ids.room = built.id;
        if (room) {
            ids.floor = checker.use(roomFloorMaterial(project, *room), floor.id);
            ids.ceiling = checker.use(roomCeilingMaterial(project, *room), floor.id);
        }
        out.rooms.push_back(std::move(ids));
    }
    return out;
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
BuildPlan makeBuildPlan(const Project& project, const BuildAssetSource& assets, const BuildMaterialSource& materials)
{
    BuildPlan plan;
    MaterialChecker checker(materials);
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
        plan.floorMaterials.push_back(floorMaterials(project, floor, build, checker));
        plan.floors.push_back(std::move(build));
    }
    for (auto& [id, asset] : used) plan.assets.push_back(std::move(asset));
    const bool noLibrary = std::any_of(plan.problems.begin(), plan.problems.end(),
                                       [](const BuildProblem& p) { return p.code == "no_library"; });
    // A caller that gives an assets source has a library; the materials source then speaks for itself.
    checker.finish(plan, noLibrary || static_cast<bool>(assets));
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
    params["materials_file"] = settings.writeFbx ? Json(std::string(kBuildMaterialsFileName)) : Json(nullptr);
    params["textures_folder"] = std::string(kBuildTexturesFolder);
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
    Json materialList = Json::array();
    for (const BuildMaterial& m : plan.materials) {
        Json tile = Json::object();
        tile["width"] = m.tileSize.width;
        tile["height"] = m.tileSize.height;
        Json maps = Json::object();
        maps["base_color"] = absolutePath(m.baseColor);
        maps["normal"] = absolutePath(m.normal);
        maps["roughness"] = absolutePath(m.roughness);
        maps["metallic"] = absolutePath(m.metallic);
        maps["opacity"] = m.opacity.empty() ? Json(nullptr) : Json(absolutePath(m.opacity));
        maps["occlusion"] = m.occlusion.empty() ? Json(nullptr) : Json(absolutePath(m.occlusion));
        Json material = Json::object();
        material["id"] = m.id;
        material["tile_size"] = std::move(tile);
        material["maps"] = std::move(maps);
        materialList.push_back(std::move(material));
    }
    params["materials"] = std::move(materialList);
    Json floors = Json::array();
    for (const FloorBuild& f : plan.floors) {
        const auto it = std::find_if(plan.floorMaterials.begin(), plan.floorMaterials.end(),
                                     [&f](const FloorMaterials& m) { return m.floor == f.id; });
        floors.push_back(floorJson(f, it == plan.floorMaterials.end() ? nullptr : &*it));
    }
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
        out.materialsFile = optionalText(output, "materials_file", "/output");
        out.objects = count(output, "objects", "/output");
        out.triangles = count(output, "triangles", "/output");
        const Json& counts = field(output, "counts", "/output");
        if (!counts.is_object()) throw Problem{"/output/counts", "Must be an object."};
        out.counts.wallSolids = static_cast<int>(count(counts, "wall_solids", "/output/counts"));
        out.counts.openingsCut = static_cast<int>(count(counts, "openings_cut", "/output/counts"));
        out.counts.fills = static_cast<int>(count(counts, "fills", "/output/counts"));
        out.counts.rooms = static_cast<int>(count(counts, "rooms", "/output/counts"));
        out.counts.items = static_cast<int>(count(counts, "items", "/output/counts"));
        out.counts.materials = static_cast<int>(count(counts, "materials", "/output/counts"));
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
