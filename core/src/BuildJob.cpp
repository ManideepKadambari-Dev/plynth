#include "plynth/core/BuildJob.h"

#include "plynth/core/BuildPreview.h"
#include "plynth/core/Items.h"
#include "plynth/core/Materials.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"
#include "plynth/core/Roofs.h"
#include "plynth/core/Stairs.h"
#include "plynth/core/Storeys.h"
#include "plynth/core/catalog/AssetInfo.h"

#include "PlanRingClean.h"

#include <algorithm>
#include <map>
#include <set>
#include <variant>

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

// The slot of a ring edge: the wall's face slot for a side face, the wall top slot for an end face.
std::string edgeSlot(const WallEdge& edge, const FloorMaterials* materials)
{
    if (!materials) return std::string(edge.side ? kExteriorWallSlot : kWallTopSlot);
    if (!edge.side) return materials->wallTopSlot;
    for (const WallFaceMaterial& face : materials->wallFaces) {
        if (face.wall == edge.wall && face.room == edge.room) return face.slot;
    }
    return std::string(kExteriorWallSlot);
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
        edge["slot"] = edgeSlot(e, materials);
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
    const auto slot = [](const std::string& name, std::string_view fallback) {
        return name.empty() ? std::string(fallback) : name;
    };
    Json slots = Json::object();
    slots["reveal"] = slot(ids.revealSlot, kExteriorWallSlot);
    slots["threshold"] = slot(ids.thresholdSlot, kThresholdSlot);
    slots["door"] = slot(ids.doorSlot, kDoorSlot);
    slots["frame"] = slot(ids.frameSlot, kFrameSlot);
    slots["glass"] = slot(ids.glassSlot, kGlassSlot);
    slots["handle"] = slot(ids.handleSlot, kHandleSlot);
    json["slots"] = std::move(slots);
    return json;
}

// One polygon with holes as the worker reads it.
Json polygonJson(const WorldPolygon& p)
{
    Json holes = Json::array();
    for (const std::vector<Vec2>& h : p.holes) holes.push_back(xyList(h));
    Json polygon = Json::object();
    polygon["outline"] = xyList(p.outline);
    polygon["holes"] = std::move(holes);
    return polygon;
}

// A list of polygons.
Json polygonsJson(const std::vector<WorldPolygon>& list)
{
    Json out = Json::array();
    for (const WorldPolygon& p : list) out.push_back(polygonJson(p));
    return out;
}

// One room as the worker reads it.
Json roomJson(const RoomBuild& r, const RoomMaterialIds& ids)
{
    Json json = Json::object();
    json["id"] = r.id;
    json["type"] = r.type ? Json(*r.type) : Json(nullptr);
    json["area"] = r.area;
    json["floor_z"] = r.floorZ;
    json["ceiling_z"] = r.ceilingZ;
    json["floor_material"] = textOrNull(ids.floor);
    json["ceiling_material"] = textOrNull(ids.ceiling);
    json["floor_slot"] = ids.floorSlot.empty() ? std::string(kNoRoomTypeLabel) + "_Floor" : ids.floorSlot;
    json["ceiling_slot"] = ids.ceilingSlot.empty() ? std::string(kNoRoomTypeLabel) + "_Ceiling" : ids.ceilingSlot;
    json["polygons"] = polygonsJson(r.polygons);
    json["ceiling_polygons"] = polygonsJson(r.ceilingPolygons);
    return json;
}

// The side names of the rings of every slab polygon.
Json slabSidesJson(const std::vector<SlabPolygonSides>& list)
{
    const auto names = [](const std::vector<SlabSide>& ring) {
        Json out = Json::array();
        for (SlabSide side : ring) out.push_back(std::string(slabSideName(side)));
        return out;
    };
    Json out = Json::array();
    for (const SlabPolygonSides& s : list) {
        Json holes = Json::array();
        for (const std::vector<SlabSide>& h : s.holes) holes.push_back(names(h));
        Json entry = Json::object();
        entry["outline"] = names(s.outline);
        entry["holes"] = std::move(holes);
        out.push_back(std::move(entry));
    }
    return out;
}

// A slab as the worker reads it.
Json slabJson(const SlabBuild& s, const SlabMaterialIds& ids)
{
    Json materials = Json::object();
    materials["edge"] = textOrNull(ids.edge);
    materials["well"] = textOrNull(ids.well);
    materials["underside"] = textOrNull(ids.underside);
    Json slots = Json::object();
    slots["edge"] = ids.edgeSlot.empty() ? std::string(kExteriorWallSlot) : ids.edgeSlot;
    slots["well"] = ids.wellSlot.empty() ? std::string(kStairWellSlot) : ids.wellSlot;
    slots["underside"] = ids.undersideSlot.empty() ? std::string(kSoffitSlot) : ids.undersideSlot;
    Json json = Json::object();
    json["bottom"] = s.bottom;
    json["top"] = s.top;
    json["polygons"] = polygonsJson(s.polygons);
    json["undersides"] = polygonsJson(s.undersides);
    json["sides"] = slabSidesJson(s.sides);
    json["tops"] = polygonsJson(s.tops);
    json["materials"] = std::move(materials);
    json["slots"] = std::move(slots);
    return json;
}

// A stair as the worker reads it.
Json stairJson(const StairBuild& s, const StairMaterialId* material)
{
    Json steps = Json::array();
    for (const StairStepBuild& step : s.steps) {
        Json js = Json::object();
        js["part"] = std::string(stairPartName(step.part));
        js["outline"] = xyList(step.outline);
        js["bottom"] = step.bottom;
        js["top"] = step.top;
        steps.push_back(std::move(js));
    }
    Json json = Json::object();
    json["id"] = s.id;
    json["type"] = s.type;
    json["to_floor"] = s.toFloor;
    json["rise"] = s.rise;
    json["riser_count"] = s.riserCount;
    json["riser"] = s.riser;
    json["material"] = textOrNull(material ? material->material : std::string());
    json["slot"] = material && !material->slot.empty() ? material->slot : std::string(kStairSlot);
    json["steps"] = std::move(steps);
    json["hole"] = polygonJson(s.hole);
    return json;
}

// The material of a roof part.
const std::string& roofPartMaterialId(const RoofMaterialIds& ids, RoofPart part)
{
    switch (part) {
    case RoofPart::Covering: return ids.covering;
    case RoofPart::Underside: return ids.underside;
    case RoofPart::Edge: return ids.edge;
    case RoofPart::Gable: return ids.gable;
    case RoofPart::Parapet: return ids.parapet;
    case RoofPart::Glazing: return ids.glazing;
    }
    return ids.covering;
}

// The slot of a roof part.
std::string roofPartSlotName(const RoofMaterialIds* ids, const std::string& type, RoofPart part)
{
    if (ids) {
        const std::string* slot = &ids->coveringSlot;
        switch (part) {
        case RoofPart::Covering: slot = &ids->coveringSlot; break;
        case RoofPart::Underside: slot = &ids->undersideSlot; break;
        case RoofPart::Edge: slot = &ids->edgeSlot; break;
        case RoofPart::Gable: slot = &ids->gableSlot; break;
        case RoofPart::Parapet: slot = &ids->parapetSlot; break;
        case RoofPart::Glazing: slot = &ids->glazingSlot; break;
        }
        if (!slot->empty()) return *slot;
    }
    switch (part) {
    case RoofPart::Covering: return std::string(type == "flat" ? kFlatRoofSlot : kRoofSlot);
    case RoofPart::Underside: return std::string(kSoffitSlot);
    case RoofPart::Edge: return std::string(kFasciaSlot);
    case RoofPart::Gable:
    case RoofPart::Parapet: return std::string(kExteriorWallSlot);
    case RoofPart::Glazing: return std::string(kGlassSlot);
    }
    return std::string(kRoofSlot);
}

// A roof section as the worker reads it.
Json roofJson(const RoofBuild& r, const RoofMaterialIds* ids)
{
    Json faces = Json::array();
    for (const RoofFace& f : r.faces) {
        Json holes = Json::array();
        for (const std::vector<Vec3>& h : f.holes) holes.push_back(xyzList(h));
        Json jf = Json::object();
        jf["part"] = std::string(roofPartName(f.part));
        jf["outline"] = xyzList(f.outline);
        jf["holes"] = std::move(holes);
        jf["material"] = textOrNull(ids ? roofPartMaterialId(*ids, f.part) : std::string());
        jf["slot"] = roofPartSlotName(ids, r.type, f.part);
        faces.push_back(std::move(jf));
    }
    Json json = Json::object();
    json["index"] = r.index;
    json["section"] = textOrNull(r.section);
    json["type"] = r.type;
    json["faces"] = std::move(faces);
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

// The collision shapes of one floor.
Json collisionJson(const std::vector<CollisionShape>* shapes)
{
    Json list = Json::array();
    if (!shapes) return list;
    for (const CollisionShape& shape : *shapes) {
        Json pieces = Json::array();
        for (const CollisionPiece& piece : shape.pieces) {
            Json p = Json::object();
            p["outline"] = xyList(piece.outline);
            p["bottom"] = piece.bottom;
            p["top"] = piece.top;
            pieces.push_back(std::move(p));
        }
        Json target = Json::object();
        target["kind"] = std::string(collisionTargetKindName(shape.kind));
        target["id"] = shape.id;
        Json entry = Json::object();
        entry["target"] = std::move(target);
        entry["pieces"] = std::move(pieces);
        entry["index"] = shape.index;
        Json hulls = Json::array();
        for (const std::vector<Vec3>& hull : shape.hulls) hulls.push_back(xyzList(hull));
        entry["hulls"] = std::move(hulls);
        list.push_back(std::move(entry));
    }
    return list;
}

// The trim of a floor as the worker reads it, one entry per kind.
Json finishTrimJson(const FloorFinish& finish)
{
    Json list = Json::array();
    const auto sweepsJson = [](const std::vector<Sweep>& all) {
        Json sweeps = Json::array();
        for (const Sweep& s : all) {
            Json sections = Json::array();
            for (const auto& section : s.sections) sections.push_back(xyzList(section));
            sweeps.push_back(Json{{"closed", s.closed}, {"sections", std::move(sections)}});
        }
        return sweeps;
    };
    for (const TrimBuild& t : finish.trim) {
        Json sweeps = Json::array();
        for (const Sweep& s : t.sweeps) {
            Json sections = Json::array();
            for (const auto& section : s.sections) sections.push_back(xyzList(section));
            sweeps.push_back(Json{{"closed", s.closed}, {"sections", std::move(sections)}});
        }
        Json entry = Json::object();
        entry["kind"] = std::string(trimKindName(t.kind));
        entry["plan_id"] = finish.floor;
        entry["object"] = trimObjectName(finish.floor, t.kind);
        entry["plynth_kind"] = "trim";
        entry["slot"] = finish.trimSlot;
        entry["material"] = textOrNull(finish.trimMaterial);
        entry["sweeps"] = std::move(sweeps);
        list.push_back(std::move(entry));
    }
    for (const StairRailing& r : finish.railings) {
        Json entry = Json::object();
        entry["kind"] = r.kind;
        entry["plan_id"] = r.stair;
        entry["object"] = railingObjectName(r);
        entry["plynth_kind"] = r.kind;
        entry["slot"] = finish.railingSlot;
        entry["material"] = textOrNull(finish.railingMaterial);
        entry["sweeps"] = sweepsJson(r.sweeps);
        list.push_back(std::move(entry));
    }
    return list;
}

// The set dressing props of a floor at its level as the worker reads them, numbered per room.
Json finishDressingJson(const FloorFinish& finish, double elevation)
{
    Json list = Json::array();
    std::map<Id, int> numbers;
    for (const DressingPlacement& p : finish.dressing) {
        const int number = ++numbers[p.room];
        std::string id = p.room + "_d" + (number < 10 ? "0" : "") + std::to_string(number);
        const Vec2 world = planToWorld(p.position);
        Json entry = Json::object();
        entry["id"] = std::move(id);
        entry["room"] = p.room;
        entry["rule"] = p.rule;
        entry["host"] = p.host;
        entry["asset"] = p.asset;
        entry["location"] = xyz({world.x, world.y, elevation + p.z});
        entry["rotation_z"] = -p.rotation * 3.14159265358979323846 / 180.0;
        entry["size"] = Json{{"width", p.size.width}, {"depth", p.size.depth}, {"height", p.size.height}};
        list.push_back(std::move(entry));
    }
    return list;
}

// The lights of a floor as the worker reads them.
Json finishLightsJson(const FloorFinish& finish)
{
    Json list = Json::array();
    for (const LightBuild& l : finish.lights) {
        Json entry = Json::object();
        entry["id"] = l.id;
        entry["kind"] = std::string(lightKindName(l.kind));
        entry["location"] = xyz(l.location);
        entry["power"] = l.power;
        entry["color"] = xyz(kLightColour);
        list.push_back(std::move(entry));
    }
    return list;
}

// One floor as the worker reads it.
Json floorJson(const FloorBuild& f, const FloorMaterials* materials, const std::vector<CollisionShape>* collision,
               const GeneratorFloorBuild* generators, const MaterialUsable& usable, const FloorFinish* finish)
{
    Json solids = Json::array();
    for (const WallSolid& s : f.wallSolids) {
        Json holes = Json::array();
        for (const WallRing& h : s.holes) holes.push_back(ringJson(h, materials));
        Json solid = Json::object();
        solid["height"] = s.height;
        solid["top_material"] = textOrNull(materials ? materials->wallTop : std::string());
        solid["top_slot"] = materials ? materials->wallTopSlot : std::string(kWallTopSlot);
        solid["outline"] = ringJson(s.outline, materials);
        solid["holes"] = std::move(holes);
        solids.push_back(std::move(solid));
    }
    Json openings = Json::array();
    for (std::size_t i = 0; i < f.openings.size(); ++i) {
        const bool known = materials && i < materials->openings.size();
        Json opening = openingJson(f.openings[i], known ? materials->openings[i] : OpeningMaterialIds{});
        if (finish) {
            for (const OpeningAssetPlacement& a : finish->openingAssets) {
                if (a.opening != f.openings[i].id) continue;
                Json asset = Json::object();
                asset["id"] = a.build.asset;
                asset["location"] = xyz(a.build.location);
                asset["rotation_z"] = a.build.rotationZ;
                asset["scale"] = xyz(a.build.scale);
                opening["asset"] = std::move(asset);
            }
        }
        openings.push_back(std::move(opening));
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
    json["slab"] = slabJson(f.slab, materials ? materials->slab : SlabMaterialIds{});
    Json stairs = Json::array();
    for (std::size_t i = 0; i < f.stairs.size(); ++i) {
        stairs.push_back(stairJson(f.stairs[i], materials && i < materials->stairs.size() ? &materials->stairs[i] : nullptr));
    }
    json["stairs"] = std::move(stairs);
    Json roofs = Json::array();
    for (std::size_t i = 0; i < f.roofs.size(); ++i) {
        roofs.push_back(roofJson(f.roofs[i], materials && i < materials->roofs.size() ? &materials->roofs[i] : nullptr));
    }
    json["roofs"] = std::move(roofs);
    json["collision"] = collisionJson(collision);
    json["generator_items"] = generators ? generatorJobItems(*generators, usable) : Json::array();
    if (finish && (!finish->trim.empty() || !finish->railings.empty())) json["trim"] = finishTrimJson(*finish);
    if (finish && !finish->dressing.empty()) json["dressing"] = finishDressingJson(*finish, f.elevation);
    if (finish && !finish->lights.empty()) json["lights"] = finishLightsJson(*finish);
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
        std::string who;
        if (resolved.origin == MaterialOrigin::Wall) who = "Wall " + resolved.from;
        else if (resolved.origin == MaterialOrigin::Room) who = "Room " + resolved.from;
        else if (resolved.origin == MaterialOrigin::Stair) who = "Stair " + resolved.from;
        else if (resolved.origin == MaterialOrigin::Roof) who = "Roof section " + resolved.from;
        else who = "The project default for " + std::string(surfaceName(resolved.surface));
        return check(*resolved.material, resolved.from, resolved.from.empty() ? Id() : floor, who);
    }

    // The id to put in the job for a material a generator item names, or empty for the neutral look.
    std::string useForItem(const std::string& id, const Id& item, const Id& floor)
    {
        if (id.empty()) return {};
        return check(id, item, floor, "Item " + item);
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
    // Looks a material up once, keeps it when usable and notes one problem per material and object otherwise.
    std::string check(const std::string& id, const Id& object, const Id& floor, const std::string& who)
    {
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
        const auto key = std::make_pair(id, object);
        if (problems_.count(key) != 0) return {};
        BuildProblem p;
        p.severity = BuildSeverity::Warning;
        p.step = std::string(kCheckStep);
        p.object = object;
        p.floor = floor;
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

    const BuildMaterialSource& source_;
    std::map<std::string, std::optional<BuildMaterialState>> cache_;
    std::map<std::string, BuildMaterial> usable_;
    std::map<std::pair<std::string, Id>, BuildProblem> problems_;
    bool planUsesMaterial_ = false;
};

// Works out slot names. Every surface first registers its base name, material and neutral surface; a base name
// that shows more than one material then gets the material added to each use (MaterialSlots.h).
class SlotNamer {
public:
    // Registers a use and returns its base name for now.
    std::string use(std::string base, const std::string& material, std::string_view surface)
    {
        uses_[base].emplace(material, std::string(surface));
        return base;
    }

    // The final name of a base with its material. A base that was never registered (a part the opening does
    // not have) keeps its name.
    std::string name(const std::string& base, const std::string& material) const
    {
        const auto found = uses_.find(base);
        if (found == uses_.end() || found->second.size() < 2) return base;
        return slotNameWithMaterial(base, material);
    }

    // Every registered slot by final name.
    std::vector<BuildSlot> table() const
    {
        std::vector<BuildSlot> out;
        for (const auto& [base, materials] : uses_) {
            for (const auto& [material, surface] : materials) {
                out.push_back({materials.size() < 2 ? base : slotNameWithMaterial(base, material), material, surface});
            }
        }
        return out;
    }

    // Every name a registered base could have: the base itself and the base with each of its materials.
    std::vector<std::string> allNames() const
    {
        std::vector<std::string> out;
        for (const auto& [base, materials] : uses_) {
            out.push_back(base);
            for (const auto& [material, surface] : materials) out.push_back(slotNameWithMaterial(base, material));
        }
        return out;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> uses_;
};

// The longest a generator or parameter id may be in a slot name, after PascalCase.
constexpr std::size_t kSlotPartLength = 24;

// A copy of a name in lower case, so names can be compared with case ignored.
std::string folded(std::string_view name)
{
    std::string out(name);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// A generator or parameter id as it stands in a slot name: PascalCase, cut to kSlotPartLength characters.
std::string slotPart(std::string_view id)
{
    std::string part = slotPascalCase(id, kGeneratorSlotLabel);
    if (part.size() > kSlotPartLength) part.resize(kSlotPartLength);
    return part;
}

// Says whether a generator's part of a slot name needs "Generator" added, so that it meets no surface's slot.
bool reservedSlotLabel(const std::string& generator, const std::string& param)
{
    static const std::set<std::string> generators{"Door", "Frame", "Glass",    "Handle", "Threshold", "Stair",    "Roof",
                                                  "Soffit", "Fascia", "Wall", "Exterior", "Flat",     "Generated"};
    return generators.count(generator) != 0 || param == "Wall" || param == "Floor" || param == "Ceiling";
}

// Names the slots of the built generator items after the surfaces have theirs (MaterialSlots.h): each
// (generator, parameter) pair in id order takes its name, or the first numbered one whose names are all free,
// and the names go into the slot table.
void nameGeneratorSlots(BuildPlan& plan, const SlotNamer& namer)
{
    using Pair = std::pair<std::string, std::string>;
    std::map<Pair, std::set<std::string>> pairs;
    bool plain = false;
    for (const GeneratorFloorBuild& floor : plan.generators.floors) {
        for (const GeneratorItemBuild& item : floor.items) {
            if (item.materialSlots.empty()) plain = true;
            for (const GeneratorMaterialSlot& slot : item.materialSlots) pairs[{item.generator, slot.param}].insert(slot.material);
        }
    }

    std::set<std::string> taken;
    for (const std::string& name : namer.allNames()) taken.insert(folded(name));
    for (const BuildSlot& slot : plan.materialSlots) taken.insert(folded(slot.name));
    taken.insert(folded(kGeneratedSlot));

    std::map<Pair, std::string> labels;
    for (const auto& [pair, materials] : pairs) {
        std::string generator = slotPart(pair.first);
        const std::string param = slotPart(pair.second);
        if (reservedSlotLabel(generator, param)) generator += kGeneratorSlotLabel;
        for (int number = 1;; ++number) {
            const std::string label = (number == 1 ? generator : generator + std::to_string(number)) + "_" + param;
            std::vector<std::string> names{label};
            for (const std::string& material : materials) names.push_back(slotNameWithMaterial(label, material));
            const bool isFree = std::none_of(names.begin(), names.end(),
                                           [&taken](const std::string& name) { return taken.count(folded(name)) != 0; });
            if (!isFree) continue;
            for (const std::string& name : names) taken.insert(folded(name));
            for (const std::string& material : materials) {
                const std::string name = materials.size() < 2 ? label : slotNameWithMaterial(label, material);
                plan.materialSlots.push_back({name, material, std::string(kGeneratedSurface)});
            }
            labels.emplace(pair, label);
            break;
        }
    }

    for (GeneratorFloorBuild& floor : plan.generators.floors) {
        for (GeneratorItemBuild& item : floor.items) {
            for (GeneratorMaterialSlot& slot : item.materialSlots) {
                const Pair pair{item.generator, slot.param};
                const std::string& label = labels.at(pair);
                slot.slot = pairs.at(pair).size() < 2 ? label : slotNameWithMaterial(label, slot.material);
            }
        }
    }
    if (plain) plan.materialSlots.push_back({std::string(kGeneratedSlot), std::string(), std::string(kGeneratedSurface)});
    std::sort(plan.materialSlots.begin(), plan.materialSlots.end(),
              [](const BuildSlot& a, const BuildSlot& b) { return a.name < b.name; });
}

// The base slot of the wall faces that look into a room, or into no room.
std::string wallFaceBase(const Room* room)
{
    return room ? roomSlotLabel(*room) + "_Wall" : std::string(kExteriorWallSlot);
}

// The neutral surface of those faces.
std::string_view wallFaceSurface(const Room* room)
{
    return room ? "Wall_Inside" : "Wall_Outside";
}

// The materials and slots of every built surface of one floor.
FloorMaterials floorMaterials(const Project& project, const Floor& floor, const FloorBuild& build, MaterialChecker& checker,
                              SlotNamer& namer)
{
    FloorMaterials out;
    out.floor = floor.id;
    out.wallTop = checker.use(surfaceDefaultMaterial(project, Surface::WallTop), floor.id);
    out.wallTopSlot = namer.use(std::string(kWallTopSlot), out.wallTop, "Wall_Top");

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
        std::string material = checker.use(wallFaceMaterial(project, *wall, room), floor.id);
        std::string slot = namer.use(wallFaceBase(room), material, wallFaceSurface(room));
        out.wallFaces.push_back({wallId, roomId, std::move(material), std::move(slot)});
    }

    for (const OpeningBuild& built : build.openings) {
        const Opening* opening = findOpening(floor, built.id);
        OpeningMaterialIds ids;
        ids.opening = built.id;
        if (opening) {
            const OpeningMaterials m = openingMaterials(project, floor, *opening);
            const OpeningRooms sides = openingRooms(floor, *opening);
            const Room* left = sides.left ? findRoom(floor, *sides.left) : nullptr;
            const Room* right = sides.right ? findRoom(floor, *sides.right) : nullptr;
            const Room* revealRoom = right ? right : left;
            ids.reveal = checker.use(m.reveal, floor.id);
            ids.revealSlot = namer.use(wallFaceBase(revealRoom), ids.reveal, wallFaceSurface(revealRoom));
            if (built.sill == 0.0) {
                ids.threshold = checker.use(m.threshold, floor.id);
                const Room* swingInto = opening->swing == WallSide::Right ? right : left;
                const Room* other = opening->swing == WallSide::Right ? left : right;
                const Room* follows = swingInto ? swingInto : other;
                if (follows) ids.thresholdSlot = namer.use(roomSlotLabel(*follows) + "_Floor", ids.threshold, "Floor");
                else ids.thresholdSlot = namer.use(std::string(kThresholdSlot), ids.threshold, "Threshold");
            }
            if (built.type == OpeningType::Door) {
                ids.door = checker.use(m.door, floor.id);
                ids.handle = checker.use(m.handle, floor.id);
                ids.doorSlot = namer.use(std::string(kDoorSlot), ids.door, "Door");
                ids.handleSlot = namer.use(std::string(kHandleSlot), ids.handle, "Handle");
            }
            if (built.type == OpeningType::Window) {
                ids.glass = checker.use(m.glass, floor.id);
                ids.glassSlot = namer.use(std::string(kGlassSlot), ids.glass, "Glass");
            }
            if (built.type != OpeningType::Plain) {
                ids.frame = checker.use(m.frame, floor.id);
                ids.frameSlot = namer.use(std::string(kFrameSlot), ids.frame, "Frame");
            }
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
            ids.floorSlot = namer.use(roomSlotLabel(*room) + "_Floor", ids.floor, "Floor");
            ids.ceilingSlot = namer.use(roomSlotLabel(*room) + "_Ceiling", ids.ceiling, "Ceiling");
        }
        out.rooms.push_back(std::move(ids));
    }

    const SlabMaterials slab = slabMaterials(project);
    bool hasEdge = !build.slab.tops.empty();
    bool hasWell = false;
    for (const SlabPolygonSides& sides : build.slab.sides) {
        const auto look = [&](const std::vector<SlabSide>& ring) {
            for (SlabSide side : ring) (side == SlabSide::Well ? hasWell : hasEdge) = true;
        };
        look(sides.outline);
        for (const std::vector<SlabSide>& hole : sides.holes) look(hole);
    }
    if (hasEdge) {
        out.slab.edge = checker.use(slab.edge, floor.id);
        out.slab.edgeSlot = namer.use(std::string(kExteriorWallSlot), out.slab.edge, "Wall_Outside");
    }
    if (hasWell) {
        out.slab.well = checker.use(slab.well, floor.id);
        out.slab.wellSlot = namer.use(std::string(kStairWellSlot), out.slab.well, "Wall_Inside");
    }
    if (!build.slab.undersides.empty()) {
        out.slab.underside = checker.use(slab.underside, floor.id);
        out.slab.undersideSlot = namer.use(std::string(kSoffitSlot), out.slab.underside, "Soffit");
    }

    for (const StairBuild& built : build.stairs) {
        const Stair* stair = findStair(floor, built.id);
        StairMaterialId ids;
        ids.stair = built.id;
        if (stair) {
            ids.material = checker.use(stairMaterial(project, *stair), floor.id);
            ids.slot = namer.use(std::string(kStairSlot), ids.material, "Stair");
        }
        out.stairs.push_back(std::move(ids));
    }

    const std::vector<RoofArea> areas = roofAreasOf(project.plan, floor.id);
    for (const RoofBuild& built : build.roofs) {
        RoofMaterialIds ids;
        ids.index = built.index;
        const auto area = std::find_if(areas.begin(), areas.end(), [&](const RoofArea& a) { return a.index == built.index; });
        if (area != areas.end()) {
            RoofArea resolved = *area;
            resolved.settings.type = built.type;
            const auto has = [&](RoofPart part) {
                return std::any_of(built.faces.begin(), built.faces.end(), [&](const RoofFace& f) { return f.part == part; });
            };
            const std::string covering = built.type == "flat" ? std::string(kFlatRoofSlot) : std::string(kRoofSlot);
            const auto add = [&](RoofPart part, std::string& material, std::string& slot, const std::string& base,
                                 std::string_view surface) {
                if (!has(part)) return;
                material = checker.use(roofPartMaterial(project, resolved, part), floor.id);
                slot = namer.use(base, material, surface);
            };
            add(RoofPart::Covering, ids.covering, ids.coveringSlot, covering, covering);
            add(RoofPart::Underside, ids.underside, ids.undersideSlot, std::string(kSoffitSlot), "Soffit");
            add(RoofPart::Edge, ids.edge, ids.edgeSlot, std::string(kFasciaSlot), "Fascia");
            add(RoofPart::Gable, ids.gable, ids.gableSlot, std::string(kExteriorWallSlot), "Wall_Outside");
            add(RoofPart::Parapet, ids.parapet, ids.parapetSlot, std::string(kExteriorWallSlot), "Wall_Outside");
            add(RoofPart::Glazing, ids.glazing, ids.glazingSlot, std::string(kGlassSlot), "Glass");
        }
        out.roofs.push_back(std::move(ids));
    }
    return out;
}

// Gives every slot field its final name and fills the plan's slot table. A part an opening or room does not
// have gets a fixed name and a neutral entry of its own.
void finishSlots(BuildPlan& plan, const SlotNamer& namer)
{
    std::vector<BuildSlot> table = namer.table();
    std::set<std::string> known;
    for (const BuildSlot& slot : table) known.insert(slot.name);
    const auto fix = [&](std::string& slot, std::string_view fallback, const std::string& material, std::string_view surface) {
        if (slot.empty()) slot = std::string(fallback);
        slot = namer.name(slot, material);
        if (known.insert(slot).second) table.push_back({slot, std::string(), std::string(surface)});
    };
    for (FloorMaterials& floor : plan.floorMaterials) {
        fix(floor.wallTopSlot, kWallTopSlot, floor.wallTop, "Wall_Top");
        for (WallFaceMaterial& face : floor.wallFaces) fix(face.slot, kExteriorWallSlot, face.material, "Wall_Inside");
        for (OpeningMaterialIds& o : floor.openings) {
            fix(o.revealSlot, kExteriorWallSlot, o.reveal, "Wall_Inside");
            fix(o.thresholdSlot, kThresholdSlot, o.threshold, "Threshold");
            fix(o.doorSlot, kDoorSlot, o.door, "Door");
            fix(o.frameSlot, kFrameSlot, o.frame, "Frame");
            fix(o.glassSlot, kGlassSlot, o.glass, "Glass");
            fix(o.handleSlot, kHandleSlot, o.handle, "Handle");
        }
        for (RoomMaterialIds& r : floor.rooms) {
            fix(r.floorSlot, "Room_Floor", r.floor, "Floor");
            fix(r.ceilingSlot, "Room_Ceiling", r.ceiling, "Ceiling");
        }
        fix(floor.slab.edgeSlot, kExteriorWallSlot, floor.slab.edge, "Wall_Outside");
        fix(floor.slab.wellSlot, kStairWellSlot, floor.slab.well, "Wall_Inside");
        fix(floor.slab.undersideSlot, kSoffitSlot, floor.slab.underside, "Soffit");
        for (StairMaterialId& stair : floor.stairs) fix(stair.slot, kStairSlot, stair.material, "Stair");
        for (RoofMaterialIds& roof : floor.roofs) {
            fix(roof.coveringSlot, kRoofSlot, roof.covering, "Roof");
            fix(roof.undersideSlot, kSoffitSlot, roof.underside, "Soffit");
            fix(roof.edgeSlot, kFasciaSlot, roof.edge, "Fascia");
            fix(roof.gableSlot, kExteriorWallSlot, roof.gable, "Wall_Outside");
            fix(roof.parapetSlot, kExteriorWallSlot, roof.parapet, "Wall_Outside");
            fix(roof.glazingSlot, kGlassSlot, roof.glazing, "Glass");
        }
    }
    for (FloorFinish& finish : plan.finish) {
        if (!finish.trim.empty()) fix(finish.trimSlot, kTrimSlot, finish.trimMaterial, "Trim");
        if (!finish.railings.empty()) fix(finish.railingSlot, kRailingSlot, finish.railingMaterial, "Railing");
    }
    std::sort(table.begin(), table.end(), [](const BuildSlot& a, const BuildSlot& b) { return a.name < b.name; });
    plan.materialSlots = std::move(table);
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

// Works out the build plan of a project without generators.
BuildPlan makeBuildPlan(const Project& project, const BuildAssetSource& assets, const BuildMaterialSource& materials)
{
    return makeBuildPlan(project, assets, materials, nullptr, std::nullopt);
}

// Works out the build plan of a project with the library's generators.
BuildPlan makeBuildPlan(const Project& project, const BuildAssetSource& assets, const BuildMaterialSource& materials,
                        const GeneratorLibrary* generators, const std::optional<AddonCache>& addons,
                        const BuildChoices* choices)
{
    const std::uint32_t seed = project.settings.variation.seed;
    static const std::vector<AssetChoice> noChoices;
    BuildPlan plan;
    MaterialChecker checker(materials);
    SlotNamer namer;
    const std::vector<double> levels = floorElevations(project.plan);

    // Without an assets source there is no library at all; a null library of generators is an empty one.
    const GeneratorLibrary none{};
    const GeneratorLibrary* library = assets ? (generators ? generators : &none) : nullptr;
    GeneratorBuildPlan made = makeGeneratorBuildPlan(project.plan, library, addons);
    std::size_t nextProblem = 0;

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
        // Everything worked out for this floor below, up to its collision, is watched for broken outlines.
        RingFaultScope ringFaults;
        FloorBuild build;
        build.id = floor.id;
        build.elevation = level;
        build.ceilingHeight = floor.ceilingHeight;
        build.wallSolids = wallSolids(floor);

        std::vector<BuildProblem> stairProblemList;
        {
            std::set<Id> refused;
            for (const StairProblem& problem : stairProblems(project.plan, floor)) {
                if (problem.error) refused.insert(problem.stair);
                stairProblemList.push_back(checkProblem(problem.code, problem.error ? BuildSeverity::Error : BuildSeverity::Warning,
                                                        problem.message, floor.id, problem.stair));
            }
            for (const Stair& stair : floor.stairs) {
                if (refused.count(stair.id)) continue;
                const StairLayout layout = stairLayout(project.plan, floor, stair);
                if (!layout.status.ok()) continue;
                StairBuild built = stairBuild(layout, level);
                built.type = stair.type;
                build.stairs.push_back(std::move(built));
            }
        }
        std::vector<WorldPolygon> floorHoles;
        if (!plan.floors.empty()) {
            for (const StairBuild& below : plan.floors.back().stairs) {
                if (below.toFloor == floor.id) floorHoles.push_back(below.hole);
            }
        }
        std::vector<WorldPolygon> ceilingHoles;
        for (const StairBuild& own : build.stairs) ceilingHoles.push_back(own.hole);

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
            std::optional<RoomBuild> made = roomBuild(floor, room, level, floorHoles, ceilingHoles);
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
        const auto addWarnings = [&](const Item& item) {
            for (const ItemProblem& problem : itemWarnings[item.id]) {
                plan.problems.push_back(checkProblem(std::string(itemProblemName(problem.kind)), BuildSeverity::Warning,
                                                     problem.message, floor.id, item.id));
            }
        };
        GeneratorFloorBuild* generatorFloor = index < made.floors.size() ? &made.floors[index] : nullptr;
        std::size_t nextBuilt = 0;
        for (const Item& item : floor.items) {
            if (!assets) continue;
            if (isGeneratorItem(item)) {
                while (nextProblem < made.problems.size() && made.problems[nextProblem].floor == floor.id &&
                       made.problems[nextProblem].object == item.id) {
                    plan.problems.push_back(made.problems[nextProblem++]);
                }
                const bool built = generatorFloor && nextBuilt < generatorFloor->items.size() &&
                                   generatorFloor->items[nextBuilt].id == item.id;
                if (!built) continue;
                ++nextBuilt;
                addWarnings(item);
                continue;
            }
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
            used.emplace(item.asset, BuildAsset{item.asset, state->blendFile, state->materials});
            const ItemPlacement placement = itemPlacement(item, level);
            ItemBuild built;
            built.id = item.id;
            built.asset = item.asset;
            built.location = placement.location;
            built.rotationZ = placement.rotationZ;
            built.size = item.size;
            built.wall = item.wall;
            build.items.push_back(std::move(built));
            addWarnings(item);
        }
        while (nextProblem < made.problems.size() && made.problems[nextProblem].floor == floor.id) {
            plan.problems.push_back(made.problems[nextProblem++]);
        }
        if (generatorFloor) {
            for (GeneratorItemBuild& item : generatorFloor->items) {
                for (const std::string& param : item.materialParams) {
                    const GeneratorValue* value = findGeneratorValue(item.params, param);
                    const std::string* text = value ? std::get_if<std::string>(value) : nullptr;
                    const std::string material = text ? checker.useForItem(*text, item.id, floor.id) : std::string();
                    item.materialSlots.push_back({param, material, std::string()});
                }
            }
        }
        for (BuildProblem& problem : stairProblemList) plan.problems.push_back(std::move(problem));

        if (index > 0) build.slabBelow = project.plan.floors[index - 1].slabThickness;
        build.slab = slabBuild(project.plan, index, levels);
        FloorRoofs roofs = floorRoofs(project.plan, index, levels);
        build.roofs = std::move(roofs.roofs);
        for (const RoofProblem& problem : roofs.problems) {
            plan.problems.push_back(checkProblem(problem.code, BuildSeverity::Warning, problem.message, floor.id, problem.object));
        }
        FloorFinish finish;
        finish.floor = floor.id;
        {
            std::vector<WorldPolygon> atFloor = floorHoles;
            atFloor.insert(atFloor.end(), ceilingHoles.begin(), ceilingHoles.end());
            finish.trim = trimBuild(floor, build, project.settings.trim, atFloor, ceilingHoles);
            for (const TrimBuild& t : finish.trim) {
                if (!t.profileMissing) continue;
                const std::string kind(trimKindName(t.kind));
                plan.problems.push_back(checkProblem("trim_profile_missing", BuildSeverity::Warning,
                                                     "The " + kind + " profile " + trimStyle(project.settings.trim, t.kind).profile +
                                                         " is not installed, so the square profile is used.",
                                                     floor.id, floor.id));
            }
            if (!finish.trim.empty()) {
                finish.trimMaterial = checker.use(surfaceDefaultMaterial(project, Surface::Trim), floor.id);
                finish.trimSlot = namer.use(std::string(kTrimSlot), finish.trimMaterial, "Trim");
            }
        }
        for (const OpeningBuild& built : build.openings) {
            const Opening* opening = findOpening(floor, built.id);
            if (!opening || !assets) continue;
            const std::vector<AssetChoice>& offered = choices ? choices->assets : noChoices;
            const std::optional<std::string> chosen = chooseOpeningAsset(*opening, opening->autoFrame && choices, offered, seed);
            if (!chosen) continue;
            const std::optional<BuildAssetState> state = assets(*chosen);
            if (!state || state->broken) {
                std::string message = "Opening " + opening->id + " names the asset " + *chosen;
                if (!state) message += ", which is not in the library";
                else message += ", which is broken" + (state->problem.empty() ? std::string() : ": " + state->problem);
                message += ", so it gets the fill Plynth builds.";
                plan.problems.push_back(checkProblem(state ? "asset_broken" : "missing_asset", BuildSeverity::Error, message,
                                                     floor.id, opening->id));
                continue;
            }
            ItemSize size{built.width, kOpeningFrameWidth, built.height};
            if (state->size) {
                size = *state->size;
            } else {
                for (const AssetChoice& c : offered) {
                    if (c.id == *chosen) size = c.size;
                }
            }
            if (opening->frame && openingAssetStretch(*opening, size) > kMaxOpeningAssetStretch + 1e-9) {
                plan.problems.push_back(checkProblem("stretched_frame", BuildSeverity::Warning,
                                                     "The asset " + *chosen + " is stretched more than 10 percent to fill opening " +
                                                         opening->id + ".",
                                                     floor.id, opening->id));
            }
            const OpeningRooms sides = openingRooms(floor, *opening);
            const bool outsideOnRight = !(sides.right && !sides.left);
            finish.openingAssets.push_back({built.id, openingAssetBuild(built, level, *chosen, size, outsideOnRight)});
            used.emplace(*chosen, BuildAsset{*chosen, state->blendFile, state->materials});
        }
        if (choices && assets) {
            for (const RoomBuild& built : build.rooms) {
                const Room* room = findRoom(floor, built.id);
                if (!room || !room->dressing) continue;
                for (DressingPlacement& prop : dressRoom(floor, *room, choices->assets, seed)) {
                    const std::optional<BuildAssetState> state = assets(prop.asset);
                    if (!state || state->broken) continue;
                    used.emplace(prop.asset, BuildAsset{prop.asset, state->blendFile, state->materials});
                    finish.dressing.push_back(std::move(prop));
                }
            }
        }
        finish.lights = floorLights(floor, level);
        plan.finish.push_back(std::move(finish));
        plan.floorMaterials.push_back(floorMaterials(project, floor, build, checker, namer));
        plan.collision.push_back(floorCollision(floor, build));
        if (ringFaults.any()) {
            plan.problems.push_back(checkProblem("outline_failed", BuildSeverity::Error,
                                                 "An outline of floor " + floor.id +
                                                     " could not be worked out, so its slab or roof would be wrong. Report this plan.",
                                                 floor.id, {}));
        }
        plan.floors.push_back(std::move(build));
    }
    while (nextProblem < made.problems.size()) plan.problems.push_back(made.problems[nextProblem++]);

    // Railings and guards need every floor, so they come once all floors are worked out.
    for (StairRailing& railing : stairRailings(project.plan, project.settings.trim, levels)) {
        const auto index = floorIndex(project.plan, railing.floor);
        if (!index || *index >= plan.finish.size()) continue;
        FloorFinish& finish = plan.finish[*index];
        if (railing.profileMissing) {
            plan.problems.push_back(checkProblem("trim_profile_missing", BuildSeverity::Warning,
                                                 "The railing profile " + project.settings.trim.railing.profile +
                                                     " is not installed, so the square profile is used.",
                                                 railing.floor, railing.stair));
        }
        if (finish.railings.empty()) {
            finish.railingMaterial = checker.use(surfaceDefaultMaterial(project, Surface::Railing), railing.floor);
            finish.railingSlot = namer.use(std::string(kRailingSlot), finish.railingMaterial, "Railing");
        }
        CollisionShape shape;
        shape.kind = railing.kind == "guard" ? CollisionTargetKind::Guard : CollisionTargetKind::Railing;
        shape.id = railing.stair;
        shape.pieces = railing.collision;
        if (!shape.pieces.empty() && *index < plan.collision.size()) plan.collision[*index].push_back(std::move(shape));
        finish.railings.push_back(std::move(railing));
    }
    plan.generators = std::move(made);
    for (auto& [id, asset] : used) plan.assets.push_back(std::move(asset));
    const bool noLibrary = std::any_of(plan.problems.begin(), plan.problems.end(),
                                       [](const BuildProblem& p) { return p.code == "no_library"; });
    // A caller that gives an assets source has a library; the materials source then speaks for itself.
    checker.finish(plan, noLibrary || static_cast<bool>(assets));
    finishSlots(plan, namer);
    nameGeneratorSlots(plan, namer);
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
    params["preview_folder"] = settings.writeFbx ? Json(std::string(kBuildPreviewFolder)) : Json(nullptr);
    params["preview_textures_folder"] = std::string(kBuildPreviewTexturesFolder);
    params["preview_texture_size"] = kPreviewTextureSize;
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
        Json assetMaterials = Json::array();
        for (const BuildAssetMaterial& m : a.materials) {
            Json maps = Json::object();
            maps["base_color"] = absolutePath(m.baseColor);
            maps["normal"] = absolutePath(m.normal);
            maps["roughness"] = absolutePath(m.roughness);
            maps["metallic"] = absolutePath(m.metallic);
            maps["opacity"] = m.opacity.empty() ? Json(nullptr) : Json(absolutePath(m.opacity));
            maps["occlusion"] = m.occlusion.empty() ? Json(nullptr) : Json(absolutePath(m.occlusion));
            Json material = Json::object();
            material["name"] = m.name;
            material["maps"] = std::move(maps);
            assetMaterials.push_back(std::move(material));
        }
        asset["materials"] = std::move(assetMaterials);
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
    Json slotList = Json::array();
    for (const BuildSlot& slot : plan.materialSlots) {
        slotList.push_back(Json{{"name", slot.name}, {"material", textOrNull(slot.material)}, {"surface", slot.surface}});
    }
    params["slots"] = std::move(slotList);
    params["generators"] = generatorJobList(plan.generators);
    std::set<std::string> listed;
    for (const BuildMaterial& m : plan.materials) listed.insert(m.id);
    const MaterialUsable usable = [&listed](std::string_view id) { return listed.count(std::string(id)) != 0; };
    Json floors = Json::array();
    for (std::size_t index = 0; index < plan.floors.size(); ++index) {
        const FloorBuild& f = plan.floors[index];
        const auto it = std::find_if(plan.floorMaterials.begin(), plan.floorMaterials.end(),
                                     [&f](const FloorMaterials& m) { return m.floor == f.id; });
        const std::vector<CollisionShape>* collision = index < plan.collision.size() ? &plan.collision[index] : nullptr;
        const auto generators = std::find_if(plan.generators.floors.begin(), plan.generators.floors.end(),
                                             [&f](const GeneratorFloorBuild& g) { return g.floor == f.id; });
        const auto finish = std::find_if(plan.finish.begin(), plan.finish.end(), [&f](const FloorFinish& x) { return x.floor == f.id; });
        floors.push_back(floorJson(f, it == plan.floorMaterials.end() ? nullptr : &*it, collision,
                                   generators == plan.generators.floors.end() ? nullptr : &*generators, usable,
                                   finish == plan.finish.end() ? nullptr : &*finish));
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
        out.counts.slabs = static_cast<int>(count(counts, "slabs", "/output/counts"));
        out.counts.stairs = static_cast<int>(count(counts, "stairs", "/output/counts"));
        out.counts.roofs = static_cast<int>(count(counts, "roofs", "/output/counts"));
        out.counts.collision = static_cast<int>(count(counts, "collision", "/output/counts"));
        if (counts.contains("generators")) out.counts.generators = static_cast<int>(count(counts, "generators", "/output/counts"));
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
