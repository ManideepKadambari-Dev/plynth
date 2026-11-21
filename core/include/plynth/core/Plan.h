#pragma once

#include "Status.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Plan coordinates are in metres. x points right and y points down, the same way as the plan image,
// so image pixel (px, py) is at (px / pixels_per_metre, py / pixels_per_metre). The image's top left
// corner is at (0, 0). The Blender side turns this into its own y up axes.

// A stable identifier such as "p12". Unique across the whole plan, never changed once given.
// Made of a letter followed by letters, digits, "_" or "-".
using Id = std::string;

inline constexpr std::string_view kFloorIdPrefix = "f";
inline constexpr std::string_view kPointIdPrefix = "p";
inline constexpr std::string_view kWallIdPrefix = "w";
inline constexpr std::string_view kRoomIdPrefix = "r";
inline constexpr std::string_view kOpeningIdPrefix = "o";

inline constexpr double kDefaultCeilingHeight = 3.0;
inline constexpr double kDefaultWallThickness = 0.2;
inline constexpr double kDefaultOpeningWidth = 0.9;
inline constexpr double kDefaultOpeningHeight = 2.1;

// How image pixels relate to metres. Empty until the user sets the scale.
struct Scale {
    std::optional<double> pixelsPerMetre;

    bool operator==(const Scale&) const = default;
};

// A corner where walls meet.
struct Point {
    Id id;
    double x = 0.0;
    double y = 0.0;
    double cornerRadius = 0.0;

    bool operator==(const Point&) const = default;
};

// A wall between two points of the same floor.
// bulge bends the wall into an arc: the signed distance in metres from the middle of the straight line
// to the middle of the arc. Positive moves it along (-dy, dx), where (dx, dy) runs from "from" to "to".
// height empty means the floor's ceiling height.
struct Wall {
    Id id;
    Id from;
    Id to;
    double thickness = kDefaultWallThickness;
    std::optional<double> height;
    double bulge = 0.0;
    std::optional<std::string> materialInside;
    std::optional<std::string> materialOutside;

    bool operator==(const Wall&) const = default;
};

// An enclosed area bounded by walls, with a room type such as "kitchen" once the user picks one.
struct Room {
    Id id;
    std::optional<std::string> type;
    std::vector<Id> walls;
    std::optional<std::string> floorMaterial;
    std::optional<std::string> ceilingMaterial;

    bool operator==(const Room&) const = default;
};

enum class OpeningType { Door, Window, Plain };

// A door, window or plain cutout in a wall.
// offset is measured along the wall from its "from" point to the centre of the opening.
// sill is the height of the bottom of the opening above the floor. shape names an entry in the
// opening shape registry, for example "rectangle" or "round_arch".
struct Opening {
    Id id;
    Id wall;
    double offset = 0.0;
    OpeningType type = OpeningType::Door;
    std::string shape = "rectangle";
    double width = kDefaultOpeningWidth;
    double height = kDefaultOpeningHeight;
    double sill = 0.0;
    std::optional<std::string> frame;

    bool operator==(const Opening&) const = default;
};

// One storey. image is a path in UTF-8 with forward slashes, relative to the project folder
// (for example "plans/floor1.png"). It may be absolute only in memory, before the project is first saved.
struct Floor {
    Id id;
    std::string name;
    std::optional<std::string> image;
    double ceilingHeight = kDefaultCeilingHeight;
    std::vector<Point> points;
    std::vector<Wall> walls;
    std::vector<Room> rooms;
    std::vector<Opening> openings;

    bool operator==(const Floor&) const = default;
};

// Everything the user marks on the plan. Saved as plan.json.
struct Plan {
    Scale scale;
    std::vector<Floor> floors;

    bool operator==(const Plan&) const = default;
};

// Makes a plan with one empty floor "f1" named "Floor 1", no image and no scale.
Plan makeEmptyPlan();

// Finds a floor by its id. Null when there is none.
Floor* findFloor(Plan& plan, std::string_view floorId);
const Floor* findFloor(const Plan& plan, std::string_view floorId);

// Finds the floor that holds the point, wall, room or opening with this id. Null when there is none.
Floor* findFloorContaining(Plan& plan, std::string_view objectId);
const Floor* findFloorContaining(const Plan& plan, std::string_view objectId);

// Find one object on a floor by its id. Null when there is none.
Point* findPoint(Floor& floor, std::string_view id);
const Point* findPoint(const Floor& floor, std::string_view id);
Wall* findWall(Floor& floor, std::string_view id);
const Wall* findWall(const Floor& floor, std::string_view id);
Room* findRoom(Floor& floor, std::string_view id);
const Room* findRoom(const Floor& floor, std::string_view id);
Opening* findOpening(Floor& floor, std::string_view id);
const Opening* findOpening(const Floor& floor, std::string_view id);

// Says whether any floor or object in the plan already uses this id.
bool idInUse(const Plan& plan, std::string_view id);

// Makes a new unused id: the prefix followed by one more than the highest number already used with it.
// An id freed by a removal can be given out again only after it is no longer the highest.
Id newId(const Plan& plan, std::string_view prefix);

// Says whether text is a well formed id.
bool isValidId(std::string_view id);

// Checks every rule of the plan format and returns the first problem found, with where set to a JSON
// pointer such as "/floors/0/walls/1/to". Rules: at least one floor, ids well formed and unique across the
// plan, walls join two different points of their own floor, rooms list walls of their own floor,
// openings sit on a wall of their own floor, all numbers finite, thickness, width, height, ceiling height
// and pixels per metre above zero, corner radius, offset and sill zero or more, wall height above zero when set.
Status checkPlan(const Plan& plan);

// The name used in files for an opening type: "door", "window" or "plain".
std::string_view openingTypeName(OpeningType type);

// The opening type for a name used in files. Empty when the name is unknown.
std::optional<OpeningType> openingTypeFromName(std::string_view name);

}
