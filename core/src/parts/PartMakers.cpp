#include "plynth/core/BuildParts.h"

#include <algorithm>
#include <mutex>

namespace plynth {

namespace {

struct MakerRegistry {
    std::mutex mutex;
    std::vector<std::unique_ptr<PartMaker>> makers;
};

// The one maker registry, made on first use so registrars in any file can use it while the program starts.
MakerRegistry& makerRegistry()
{
    static MakerRegistry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits or "_".
bool isWellFormedMakerId(std::string_view id)
{
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
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

// A ring of [x, y].
Json ring2(const std::vector<Vec2>& points)
{
    Json out = Json::array();
    for (const Vec2& p : points) out.push_back(xy(p));
    return out;
}

// A ring of [x, y, z].
Json ring3(const std::vector<Vec3>& points)
{
    Json out = Json::array();
    for (const Vec3& p : points) out.push_back(xyz(p));
    return out;
}

}

// Adds a maker unless its id is taken or badly formed.
bool registerPartMaker(std::unique_ptr<PartMaker> maker)
{
    if (!maker) return false;
    const std::string id(maker->id());
    if (!isWellFormedMakerId(id)) return false;
    MakerRegistry& r = makerRegistry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.makers) {
        if (existing->id() == id) return false;
    }
    r.makers.push_back(std::move(maker));
    return true;
}

// Adds the maker to the registry when the program starts.
PartMakerRegistrar::PartMakerRegistrar(std::unique_ptr<PartMaker> maker)
{
    registerPartMaker(std::move(maker));
}

// Lists the makers by order, then id.
std::vector<const PartMaker*> allPartMakers()
{
    MakerRegistry& r = makerRegistry();
    std::lock_guard lock(r.mutex);
    std::vector<const PartMaker*> out;
    for (const auto& maker : r.makers) out.push_back(maker.get());
    std::sort(out.begin(), out.end(), [](const PartMaker* a, const PartMaker* b) {
        if (a->order() != b->order()) return a->order() < b->order();
        return a->id() < b->id();
    });
    return out;
}

// One part as the worker reads it.
Json buildPartToJson(const BuildPart& part)
{
    Json prisms = Json::array();
    for (const PartPrism& p : part.prisms) {
        Json holes = Json::array();
        for (const std::vector<Vec2>& h : p.holes) holes.push_back(ring2(h));
        Json entry = Json::object();
        entry["outline"] = ring2(p.outline);
        entry["holes"] = std::move(holes);
        entry["bottom"] = p.bottom;
        entry["top"] = p.top;
        entry["slot"] = p.slot;
        entry["side_slot"] = p.sideSlot.empty() ? Json(nullptr) : Json(p.sideSlot);
        prisms.push_back(std::move(entry));
    }
    Json faces = Json::array();
    for (const PartFace& f : part.faces) {
        Json holes = Json::array();
        for (const std::vector<Vec3>& h : f.holes) holes.push_back(ring3(h));
        Json entry = Json::object();
        entry["outline"] = ring3(f.outline);
        entry["holes"] = std::move(holes);
        entry["slot"] = f.slot;
        faces.push_back(std::move(entry));
    }
    Json sweeps = Json::array();
    for (const PartSweep& s : part.sweeps) {
        Json sections = Json::array();
        for (const std::vector<Vec3>& section : s.sweep.sections) sections.push_back(ring3(section));
        Json entry = Json::object();
        entry["closed"] = s.sweep.closed;
        entry["sections"] = std::move(sections);
        entry["slot"] = s.slot;
        sweeps.push_back(std::move(entry));
    }
    Json collision = Json::array();
    for (const CollisionPiece& piece : part.collision) {
        Json entry = Json::object();
        entry["outline"] = ring2(piece.outline);
        entry["bottom"] = piece.bottom;
        entry["top"] = piece.top;
        collision.push_back(std::move(entry));
    }
    Json hulls = Json::array();
    for (const std::vector<Vec3>& hull : part.hulls) hulls.push_back(ring3(hull));

    Json json = Json::object();
    json["object"] = part.object;
    json["plynth_kind"] = part.kind;
    json["plynth_id"] = part.id;
    json["bevel"] = part.bevel;
    json["uv"] = part.uv == PartUv::Along ? "along" : "world";
    json["prisms"] = std::move(prisms);
    json["faces"] = std::move(faces);
    json["sweeps"] = std::move(sweeps);
    json["collision"] = std::move(collision);
    json["hulls"] = std::move(hulls);
    return json;
}

// One strip light as the worker reads it.
Json stripLightToJson(const StripLight& light)
{
    Json json = Json::object();
    json["id"] = light.id;
    json["start"] = xyz(light.start);
    json["end"] = xyz(light.end);
    json["facing"] = xyz(light.facing);
    json["width"] = light.width;
    json["power"] = light.power;
    json["colour"] = xyz(light.colour);
    return json;
}

}
