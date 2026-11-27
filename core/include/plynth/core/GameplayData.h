#pragma once

#include "BuildGeometry.h"
#include "BuildParts.h"
#include "FinishSettings.h"
#include "Geometry.h"
#include "Plan.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Gameplay data (DESIGN.md section 7.16): a JSON side file of the export with room volumes, door positions and
// light positions, for audio zones, navigation and spawn points. Written from the plan the build used, in
// Blender world metres (BuildGeometry.h), Z up, in the format of schema/gameplay.schema.json.

inline constexpr std::string_view kGameplayFormat = "plynth-gameplay";
inline constexpr int kGameplayVersion = 1;

// How far a spawn point keeps from furniture and walls.
inline constexpr double kSpawnClearance = 0.35;

// A straight piece of a light strip (LightStrips.h): from start to end, shining the unit direction facing,
// width across, power the watts of this piece.
struct GameplayStrip {
    Vec3 start;
    Vec3 end;
    Vec3 facing;
    double width = 0.0;
    double power = 0.0;

    bool operator==(const GameplayStrip&) const = default;
};

// A light marker, or with strip set a piece of a light strip: kind "strip" in the file, position the middle of
// the piece, and kind is then not used.
struct GameplayLight {
    Id id;
    Id floor;
    std::optional<Id> room;
    LightKind kind = LightKind::Ceiling;
    Vec3 position;
    std::optional<GameplayStrip> strip;

    bool operator==(const GameplayLight&) const = default;
};

// A convex outline in world X and Y raised from bottom to top.
struct GameplayVolume {
    std::vector<Vec2> outline;
    double bottom = 0.0;
    double top = 0.0;

    bool operator==(const GameplayVolume&) const = default;
};

// A room: its floor polygons (RoomBuild::polygons), the convex pieces of them from floor to ceiling, the box
// round them, and a spawn point: the room's label point when it is kSpawnClearance clear of furniture and wall
// faces, otherwise the nearest such place found on a 0.05 m grid, or empty when there is none.
struct GameplayRoom {
    Id id;
    Id floor;
    std::optional<std::string> type;
    double area = 0.0;
    double floorZ = 0.0;
    double ceilingZ = 0.0;
    Vec3 boundsMin;
    Vec3 boundsMax;
    std::vector<WorldPolygon> polygons;
    std::vector<GameplayVolume> volumes;
    std::optional<Vec3> spawn;

    bool operator==(const GameplayRoom&) const = default;
};

// A door, or a plain opening with sill 0 (kind "passage"). position is the bottom centre on the wall's centre
// line, along the unit direction of the wall, facing the unit direction of its right side. roomFront is the
// room on the facing side, roomBack the other; empty means outside. Windows and plain openings with a sill are
// left out.
struct GameplayDoor {
    Id id;
    Id floor;
    std::string kind;
    Vec3 position;
    Vec2 along;
    Vec2 facing;
    double width = 0.0;
    double height = 0.0;
    std::optional<Id> roomFront;
    std::optional<Id> roomBack;

    bool operator==(const GameplayDoor&) const = default;
};

struct GameplayFloor {
    Id id;
    std::string name;
    double elevation = 0.0;
    double ceilingHeight = 0.0;

    bool operator==(const GameplayFloor&) const = default;
};

struct GameplayData {
    std::string project;
    std::vector<GameplayFloor> floors;
    std::vector<GameplayRoom> rooms;
    std::vector<GameplayDoor> doors;
    std::vector<GameplayLight> lights;

    bool operator==(const GameplayData&) const = default;
};

// Works out the gameplay data of a plan from what the build makes of each floor (one FloorBuild per floor, in
// plan order, matched by id) and the lights, which are passed in as they are. Floors, rooms and doors are in
// plan order.
GameplayData gameplayData(std::string_view projectName, const Plan& plan, const std::vector<FloorBuild>& builds,
                          const std::vector<GameplayLight>& lights);

// The light markers of a plan as gameplay lights, in world coordinates at their floor's level.
std::vector<GameplayLight> planLights(const Plan& plan);

// The strip lights the build made (BuildPlan::stripLights, one list per floor in plan order) as gameplay lights,
// one per piece, with the room that holds the middle of the piece on the plan, or none.
std::vector<GameplayLight> planStripLights(const Plan& plan, const std::vector<std::vector<StripLight>>& lights);

// The data as the gameplay file's JSON.
Json gameplayToJson(const GameplayData& data);

}
