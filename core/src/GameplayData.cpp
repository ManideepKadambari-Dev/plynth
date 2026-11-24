#include "plynth/core/GameplayData.h"

#include "plynth/core/Collision.h"
#include "plynth/core/Items.h"
#include "plynth/core/Rooms.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace plynth {

namespace {

constexpr double kSpawnGrid = 0.05;

// The world position of a plan position, and back.
Vec2 worldToPlan(Vec2 world)
{
    return {world.x, -world.y};
}

// The distance from a position to the segment from a to b.
double segmentDistance(Vec2 p, Vec2 a, Vec2 b)
{
    return projectOntoSegment(p, a, b).distance;
}

// The distance from a position to the nearest edge of a ring.
double ringDistance(Vec2 p, const std::vector<Vec2>& ring)
{
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < ring.size(); ++i) best = std::min(best, segmentDistance(p, ring[i], ring[(i + 1) % ring.size()]));
    return best;
}

// Says whether a world position stands on a room's floor at least clearance from every edge and every item.
bool spawnFits(Vec2 p, const std::vector<WorldPolygon>& polygons, const std::vector<std::vector<Vec2>>& items)
{
    bool inside = false;
    for (const WorldPolygon& poly : polygons) {
        if (!insidePolygon(p, poly.outline)) continue;
        bool inHole = false;
        for (const auto& hole : poly.holes) inHole = inHole || insidePolygon(p, hole);
        if (!inHole) inside = true;
    }
    if (!inside) return false;
    for (const WorldPolygon& poly : polygons) {
        if (ringDistance(p, poly.outline) < kSpawnClearance) return false;
        for (const auto& hole : poly.holes) {
            if (ringDistance(p, hole) < kSpawnClearance) return false;
        }
    }
    for (const auto& item : items) {
        if (insidePolygon(p, item) || ringDistance(p, item) < kSpawnClearance) return false;
    }
    return true;
}

// Finds the spawn point of a room: the label point when it is clear, otherwise the nearest clear grid point.
std::optional<Vec2> findSpawn(Vec2 label, const std::vector<WorldPolygon>& polygons,
                              const std::vector<std::vector<Vec2>>& items, Vec2 low, Vec2 high)
{
    if (spawnFits(label, polygons, items)) return label;
    std::optional<Vec2> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    const int nx = static_cast<int>(std::floor((high.x - low.x) / kSpawnGrid));
    const int ny = static_cast<int>(std::floor((high.y - low.y) / kSpawnGrid));
    for (int i = 0; i <= nx; ++i) {
        for (int j = 0; j <= ny; ++j) {
            const Vec2 p{low.x + i * kSpawnGrid, low.y + j * kSpawnGrid};
            const double d = distance(p, label);
            if (d >= bestDistance - 1e-12) continue;
            if (!spawnFits(p, polygons, items)) continue;
            best = p;
            bestDistance = d;
        }
    }
    return best;
}

// The room on a floor at a world position, empty when there is none.
std::optional<Id> roomAtWorld(const Floor& floor, Vec2 world)
{
    const Id id = roomAt(floor, worldToPlan(world));
    if (id.empty()) return std::nullopt;
    return id;
}

// A world point as a JSON list.
Json pointJson(Vec2 p)
{
    return Json::array({p.x, p.y});
}

Json pointJson(Vec3 p)
{
    return Json::array({p.x, p.y, p.z});
}

// A ring as a JSON list of points.
Json ringJson(const std::vector<Vec2>& ring)
{
    Json out = Json::array();
    for (const Vec2& p : ring) out.push_back(pointJson(p));
    return out;
}

// Text or null.
Json optionalText(const std::optional<std::string>& text)
{
    return text ? Json(*text) : Json(nullptr);
}

}

// Works out the gameplay data of a plan.
GameplayData gameplayData(std::string_view projectName, const Plan& plan, const std::vector<FloorBuild>& builds,
                          const std::vector<GameplayLight>& lights)
{
    GameplayData data;
    data.project = std::string(projectName);
    data.lights = lights;
    for (const Floor& floor : plan.floors) {
        const auto found = std::find_if(builds.begin(), builds.end(), [&](const FloorBuild& b) { return b.id == floor.id; });
        if (found == builds.end()) continue;
        const FloorBuild& build = *found;
        data.floors.push_back({floor.id, floor.name, build.elevation, floor.ceilingHeight});

        std::vector<std::vector<Vec2>> itemShapes;
        for (const Item& item : floor.items) {
            const ItemFootprint fp = itemFootprint(item);
            std::vector<Vec2> ring;
            for (const Vec2& c : fp.corners) ring.push_back(planToWorld(c));
            itemShapes.push_back(std::move(ring));
        }

        for (const RoomBuild& rb : build.rooms) {
            GameplayRoom room;
            room.id = rb.id;
            room.floor = floor.id;
            room.type = rb.type;
            room.area = rb.area;
            room.floorZ = rb.floorZ;
            room.ceilingZ = rb.ceilingZ;
            room.polygons = rb.polygons;
            Vec2 low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            Vec2 high{-low.x, -low.y};
            for (const WorldPolygon& poly : rb.polygons) {
                for (const Vec2& p : poly.outline) {
                    low = {std::min(low.x, p.x), std::min(low.y, p.y)};
                    high = {std::max(high.x, p.x), std::max(high.y, p.y)};
                }
                for (const auto& piece : convexPieces(poly)) room.volumes.push_back({piece, rb.floorZ, rb.ceilingZ});
            }
            if (rb.polygons.empty()) low = high = {0.0, 0.0};
            room.boundsMin = {low.x, low.y, rb.floorZ};
            room.boundsMax = {high.x, high.y, rb.ceilingZ};
            if (const Room* marked = findRoom(floor, rb.id)) {
                const Vec2 label = planToWorld(roomShape(floor, *marked).labelPoint);
                if (const auto spawn = findSpawn(label, rb.polygons, itemShapes, low, high)) {
                    room.spawn = Vec3{spawn->x, spawn->y, rb.floorZ};
                }
            }
            data.rooms.push_back(std::move(room));
        }

        for (const OpeningBuild& ob : build.openings) {
            const bool door = ob.type == OpeningType::Door;
            const bool passage = ob.type == OpeningType::Plain && ob.sill <= 1e-9;
            if (!door && !passage) continue;
            GameplayDoor d;
            d.id = ob.id;
            d.floor = floor.id;
            d.kind = door ? "door" : "passage";
            d.position = {ob.centre.x, ob.centre.y, build.elevation + ob.sill};
            d.along = ob.tangent;
            d.facing = ob.right;
            d.width = ob.width;
            d.height = ob.height;
            const double reach = ob.thickness / 2.0 + kBuildFaceProbe;
            d.roomFront = roomAtWorld(floor, {ob.centre.x + ob.right.x * reach, ob.centre.y + ob.right.y * reach});
            d.roomBack = roomAtWorld(floor, {ob.centre.x - ob.right.x * reach, ob.centre.y - ob.right.y * reach});
            data.doors.push_back(std::move(d));
        }
    }
    return data;
}

// Writes the gameplay file's JSON.
Json gameplayToJson(const GameplayData& data)
{
    Json out;
    out["format"] = std::string(kGameplayFormat);
    out["version"] = kGameplayVersion;
    out["project"] = data.project;
    out["units"] = "metres";
    out["up"] = "Z";
    Json floors = Json::array();
    for (const GameplayFloor& f : data.floors) {
        floors.push_back(Json{{"id", f.id}, {"name", f.name}, {"elevation", f.elevation}, {"ceiling_height", f.ceilingHeight}});
    }
    out["floors"] = std::move(floors);
    Json rooms = Json::array();
    for (const GameplayRoom& r : data.rooms) {
        Json room;
        room["id"] = r.id;
        room["floor"] = r.floor;
        room["type"] = optionalText(r.type);
        room["area"] = r.area;
        room["floor_z"] = r.floorZ;
        room["ceiling_z"] = r.ceilingZ;
        room["bounds"] = Json{{"min", pointJson(r.boundsMin)}, {"max", pointJson(r.boundsMax)}};
        Json polygons = Json::array();
        for (const WorldPolygon& p : r.polygons) {
            Json holes = Json::array();
            for (const auto& h : p.holes) holes.push_back(ringJson(h));
            polygons.push_back(Json{{"outline", ringJson(p.outline)}, {"holes", std::move(holes)}});
        }
        room["polygons"] = std::move(polygons);
        Json volumes = Json::array();
        for (const GameplayVolume& v : r.volumes) {
            volumes.push_back(Json{{"outline", ringJson(v.outline)}, {"bottom", v.bottom}, {"top", v.top}});
        }
        room["volumes"] = std::move(volumes);
        room["spawn"] = r.spawn ? pointJson(*r.spawn) : Json(nullptr);
        rooms.push_back(std::move(room));
    }
    out["rooms"] = std::move(rooms);
    Json doors = Json::array();
    for (const GameplayDoor& d : data.doors) {
        Json door;
        door["id"] = d.id;
        door["floor"] = d.floor;
        door["kind"] = d.kind;
        door["position"] = pointJson(d.position);
        door["along"] = pointJson(d.along);
        door["facing"] = pointJson(d.facing);
        door["width"] = d.width;
        door["height"] = d.height;
        door["rooms"] = Json::array({optionalText(d.roomFront), optionalText(d.roomBack)});
        doors.push_back(std::move(door));
    }
    out["doors"] = std::move(doors);
    Json lights = Json::array();
    for (const GameplayLight& l : data.lights) {
        Json light;
        light["id"] = l.id;
        light["floor"] = l.floor;
        light["room"] = optionalText(l.room);
        light["kind"] = std::string(lightKindName(l.kind));
        light["position"] = pointJson(l.position);
        lights.push_back(std::move(light));
    }
    out["lights"] = std::move(lights);
    return out;
}

}
