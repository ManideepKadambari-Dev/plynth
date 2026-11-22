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
inline constexpr std::string_view kItemIdPrefix = "i";

inline constexpr double kDefaultCeilingHeight = 3.0;
inline constexpr double kDefaultWallThickness = 0.2;
inline constexpr double kDefaultOpeningWidth = 0.9;
inline constexpr double kDefaultOpeningHeight = 2.1;

// How image pixels relate to metres. Empty until the user sets the scale.
struct Scale {
    std::optional<double> pixelsPerMetre;

    bool operator==(const Scale&) const = default;
};

// A corner where walls meet. cornerRadius rounds the corner with an arc of that radius when the point joins
// exactly two straight walls (see cornerRounding in WallGeometry.h); elsewhere it has no effect.
struct Point {
    Id id;
    double x = 0.0;
    double y = 0.0;
    double cornerRadius = 0.0;

    bool operator==(const Point&) const = default;
};

// The two sides of a wall, as seen on the plan looking from its from point toward its to point. Right is the
// direction (-dy, dx), where (dx, dy) runs from "from" to "to"; with y pointing down that is the right hand side
// on screen.
enum class WallSide { Left, Right };

// A wall between two points of the same floor.
// bulge bends the wall into a circular arc: the signed distance in metres from the middle of the straight
// line to the middle of the arc. Positive bends it to the right side (see WallSide). 0 is straight.
// height empty means the floor's ceiling height.
// materialInside is used on every face of the wall that looks into a room (both faces of a wall between two
// rooms), materialOutside on every face that looks into no room. Empty: see Materials.h for what is used.
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
// wallMaterial is used on the wall faces that look into this room when the wall itself names no inside
// material (plan format 4). Materials are catalog ids; the plan keeps them even when the library lacks them.
struct Room {
    Id id;
    std::optional<std::string> type;
    std::vector<Id> walls;
    std::optional<std::string> floorMaterial;
    std::optional<std::string> wallMaterial;
    std::optional<std::string> ceilingMaterial;

    bool operator==(const Room&) const = default;
};

enum class OpeningType { Door, Window, Plain };

// Which jamb of an opening a door hangs on. Start is the jamb nearer the wall's from point, End the one
// nearer its to point.
enum class HingeEnd { Start, End };

// A door, window or plain cutout in a wall.
// offset is measured along the wall's true centre line (along the arc for a curved wall) from its "from"
// point to the centre of the opening, and width along the same line. sill is the height of the bottom of
// the opening above the floor. shape names an entry in the opening shape registry (OpeningShapes.h), for
// example "rectangle" or "round_arch". hinge and swing say where a door hangs and which side of the wall
// it opens toward; they are kept for every type but only doors use them now. frame is a catalog asset id,
// set from milestone 6 on.
struct Opening {
    Id id;
    Id wall;
    double offset = 0.0;
    OpeningType type = OpeningType::Door;
    std::string shape = "rectangle";
    double width = kDefaultOpeningWidth;
    double height = kDefaultOpeningHeight;
    double sill = 0.0;
    HingeEnd hinge = HingeEnd::Start;
    WallSide swing = WallSide::Right;
    std::optional<std::string> frame;

    bool operator==(const Opening&) const = default;
};

// The size of a placed asset in metres, copied from the catalog when it is placed or swapped, so the
// footprint, fit and overlap work without the library (a project moved to another machine). width runs along
// the asset's own x, depth along its own y (back to front), height up. All above zero.
struct ItemSize {
    double width = 0.0;
    double depth = 0.0;
    double height = 0.0;

    bool operator==(const ItemSize&) const = default;
};

// A catalog asset placed on a floor (DESIGN.md section 7.6).
// x and y are the centre of the footprint on the plan, which is where the asset's pivot (the bottom centre
// of its bounding box) goes. z is the height of the asset's bottom above the floor.
// rotation is in degrees from 0 up to 360, clockwise as seen on the plan. At 0 the asset's front faces +y
// (down the plan image); the Blender build turns it into Blender's axes as Items.h says.
// room is the room the item stands in, kept up to date by the item and room commands. Empty when it stands
// in no room.
// wall is set for a wall-mounted item (a TV, shelf or picture). Its back then lies flush against one face of
// that wall: the plan view moves it along the wall and the wall view sets its height (z). x, y and rotation
// are still stored, and the item commands keep them flush; a wall that is moved later can leave it off its
// wall, which itemProblems reports (Items.h).
struct Item {
    Id id;
    std::string asset;
    std::optional<Id> room;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rotation = 0.0;
    std::optional<Id> wall;
    ItemSize size;

    bool operator==(const Item&) const = default;
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
    std::vector<Item> items;

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

// Finds the floor that holds the point, wall, room, opening or item with this id. Null when there is none.
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
Item* findItem(Floor& floor, std::string_view id);
const Item* findItem(const Floor& floor, std::string_view id);

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
// Items: asset a well formed name, room and wall (when set) a room and a wall of their own floor, z zero or
// more, rotation from 0 up to 360, every size above zero. Whether an item fits its room or overlaps anything
// is not part of the format; itemProblems in Items.h reports that.
// Whether an opening fits its wall, and whether its shape is registered, is not part of the format: commands
// refuse openings that do not fit (checkOpeningFit in Openings.h), and openingProblems lists the ones that
// stopped fitting after a wall changed. So a file from another machine with a custom shape still loads.
Status checkPlan(const Plan& plan);

// The name used in files for an opening type: "door", "window" or "plain".
std::string_view openingTypeName(OpeningType type);

// The opening type for a name used in files. Empty when the name is unknown.
std::optional<OpeningType> openingTypeFromName(std::string_view name);

// The name used in files for a wall side: "left" or "right".
std::string_view wallSideName(WallSide side);

// The wall side for a name used in files. Empty when the name is unknown.
std::optional<WallSide> wallSideFromName(std::string_view name);

// The other side of a wall.
WallSide otherSide(WallSide side);

// The name used in files for a hinge end: "start" or "end".
std::string_view hingeEndName(HingeEnd end);

// The hinge end for a name used in files. Empty when the name is unknown.
std::optional<HingeEnd> hingeEndFromName(std::string_view name);

// The other end of an opening.
HingeEnd otherEnd(HingeEnd end);

}
