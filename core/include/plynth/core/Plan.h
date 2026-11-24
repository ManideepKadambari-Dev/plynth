#pragma once

#include "FinishSettings.h"
#include "Status.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace plynth {

// Plan coordinates are in metres. x points right and y points down, the same way as the plan image,
// so image pixel (px, py) is at (px / pixels_per_metre, py / pixels_per_metre). The image's top left
// corner is at (0, 0). The Blender side turns this into its own y up axes.

// A stable identifier such as "p12" or "p7k3q9xm2". Unique across the whole plan, never changed once given.
// Made of a letter followed by letters, digits, "_" or "-".
using Id = std::string;

inline constexpr std::string_view kFloorIdPrefix = "f";
inline constexpr std::string_view kPointIdPrefix = "p";
inline constexpr std::string_view kWallIdPrefix = "w";
inline constexpr std::string_view kRoomIdPrefix = "r";
inline constexpr std::string_view kOpeningIdPrefix = "o";
inline constexpr std::string_view kItemIdPrefix = "i";
inline constexpr std::string_view kStairIdPrefix = "s";
inline constexpr std::string_view kRoofSectionIdPrefix = "rf";
inline constexpr std::string_view kLightIdPrefix = "l";

// The characters a new id has after its prefix: digits and lower case letters without i, l, o and u, so none
// looks like another.
inline constexpr std::string_view kIdAlphabet = "0123456789abcdefghjkmnpqrstvwxyz";
// How many characters a new id has after its prefix.
inline constexpr std::size_t kNewIdLength = 8;
// How many random ids newId tries before it falls back to counting.
inline constexpr int kMaxIdDraws = 1000;

inline constexpr double kDefaultCeilingHeight = 3.0;
inline constexpr double kDefaultWallThickness = 0.2;
inline constexpr double kDefaultOpeningWidth = 0.9;
inline constexpr double kDefaultOpeningHeight = 2.1;
inline constexpr double kDefaultSlabThickness = 0.2;
inline constexpr double kDefaultStairWidth = 1.0;

// The range checkPlan accepts for a slab thickness, in metres.
inline constexpr double kMinSlabThickness = 0.05;
inline constexpr double kMaxSlabThickness = 1.0;

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
    bool dressing = true; // set dressing rules add props to this room (plan format 7)

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
    // With no frame, a rectangular door or window gets a catalog door or window that fits (OpeningAssets.h).
    // False keeps the fill Plynth builds (plan format 7).
    bool autoFrame = true;

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

// One parameter value of a placed generator (Generators.h): a number (lengths in metres, angles in degrees,
// whole numbers too), yes or no, or text (a choice id, or a material id where empty text means the neutral
// look). In plan.json a number, true or false, a string, or null for empty text.
using GeneratorValue = std::variant<double, bool, std::string>;

struct GeneratorParamValue {
    std::string id;
    GeneratorValue value;

    bool operator==(const GeneratorParamValue&) const = default;
};

// The generator an item runs instead of placing an asset (plan format 6), and its parameter values, sorted by
// id with each id once. name is the generator's id, the file stem of Library/generators/<name>.py. The plan
// keeps it even when the library on this machine does not have that generator.
struct GeneratorUse {
    std::string name;
    std::vector<GeneratorParamValue> params;

    bool operator==(const GeneratorUse&) const = default;
};

// A catalog asset placed on a floor (DESIGN.md section 7.6), or a generator placed like one (DESIGN.md 7.10).
// A generator item has generator set and asset empty; its size comes from its parameters (generatorSize in
// Generators.h). Everything else below means the same for both kinds.
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
    std::optional<GeneratorUse> generator;

    bool operator==(const Item&) const = default;
};

// Says whether an item runs a generator rather than placing an asset.
bool isGeneratorItem(const Item& item);

// Where a floor's plan image sits, in metres (plan format 5). Image pixel (px, py) of that floor is at
// (px / pixels_per_metre + x, py / pixels_per_metre + y), so an upper floor's image can be lined up with the
// floor below (imageToPlan in Geometry.h). Every floor shares the plan's one scale.
struct ImageOffset {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const ImageOffset&) const = default;
};

// Which way an L or U shaped stair turns, and which way a spiral stair winds, as seen on the plan while
// climbing.
enum class StairTurn { Left, Right };

// A stair from its floor up to the next floor in plan order (DESIGN.md section 7.8, plan format 5). It is
// marked once, on the lower floor. type names an entry of the stair type registry (Stairs.h), for example
// "straight" or "u_shape". x and y are the middle of the front edge of the bottom step. rotation is the climb
// direction in degrees from 0 up to 360, the same way as an item's front (itemFront in Items.h): at 0 the stair
// climbs toward +y, turned clockwise as seen on the plan. width is the width of each flight. The step count
// and footprint are worked out from the floor heights (stairLayout in Stairs.h). material is a catalog id;
// empty uses the project's stair default.
struct Stair {
    Id id;
    std::string type = "straight";
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double width = kDefaultStairWidth;
    StairTurn turn = StairTurn::Right;
    std::optional<std::string> material;

    bool operator==(const Stair&) const = default;
};

// A light marker (plan format 7): where the build puts a point light. x and y are plan metres, z is the height
// above the floor, from 0 to the floor's ceiling height. room is the room it stands in, kept up to date by the
// light and room commands like an item's room. No fixture is built: a lamp is a catalog asset.
struct LightMarker {
    Id id;
    LightKind kind = LightKind::Ceiling;
    std::optional<Id> room;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const LightMarker&) const = default;
};

// One storey. image is a path in UTF-8 with forward slashes, relative to the project folder
// (for example "plans/floor1.png"). It may be absolute only in memory, before the project is first saved.
// slabThickness is the slab between this floor's ceiling and the next floor (or the roof): the next floor's
// level is this floor's level plus ceilingHeight plus slabThickness (floorElevations in BuildGeometry.h).
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
    double slabThickness = kDefaultSlabThickness;
    ImageOffset imageOffset;
    std::vector<Stair> stairs;
    std::vector<LightMarker> lights;

    bool operator==(const Floor&) const = default;
};

// How a roof section is built (DESIGN.md section 7.9). type names an entry of the roof type registry
// (Roofs.h): "flat" (with a parapet), "gable", "hip", "shed" or "sawtooth". pitch is in degrees above
// horizontal, overhang is how far a pitched roof (or a flat one without a parapet) reaches past the outer wall
// faces, parapet is the height of the low wall round a flat roof (0 for none). direction is in degrees from 0
// up to 360, clockwise on the plan from +x: the ridge of a gable or sawtooth roof runs along it, and a shed
// roof falls toward it. Empty direction means automatic (see Roofs.h). Each type says which fields it uses.
struct RoofSettings {
    std::string type = "flat";
    double pitch = 30.0;
    double overhang = 0.3;
    double parapet = 0.6;
    std::optional<double> direction;

    bool operator==(const RoofSettings&) const = default;
};

// Settings for one roof section that differ from the plan's default. Roof sections are not drawn: they are
// worked out from the floors (roofAreas in Roofs.h), so an entry is tied to the section of its floor whose
// area holds the anchor point (x, y). An entry whose anchor lies in no section any more is kept and reported
// as unused. material is the catalog id of the roof covering; empty uses the project's roof or flat_roof
// default.
struct RoofSection {
    Id id;
    Id floor;
    double x = 0.0;
    double y = 0.0;
    RoofSettings settings;
    std::optional<std::string> material;

    bool operator==(const RoofSection&) const = default;
};

// Everything the user marks on the plan. Saved as plan.json. Floors are listed from the lowest up.
// roof is the default for every roof section without an entry of its own in roofSections.
struct Plan {
    Scale scale;
    std::vector<Floor> floors;
    RoofSettings roof;
    std::vector<RoofSection> roofSections;

    bool operator==(const Plan&) const = default;
};

// Makes a plan with one empty floor "f1" named "Floor 1", no image and no scale.
Plan makeEmptyPlan();

// Finds a floor by its id. Null when there is none.
Floor* findFloor(Plan& plan, std::string_view floorId);
const Floor* findFloor(const Plan& plan, std::string_view floorId);

// Finds the floor that holds the point, wall, room, opening, item, stair or light with this id. Null when there is none.
Floor* findFloorContaining(Plan& plan, std::string_view objectId);
const Floor* findFloorContaining(const Plan& plan, std::string_view objectId);

// Where a floor is in plan order, from 0 at the lowest. Empty when there is no floor with this id.
std::optional<std::size_t> floorIndex(const Plan& plan, std::string_view floorId);

// The floor right above or below one, in plan order. Null for the top or the lowest floor, or an unknown id.
const Floor* floorAbove(const Plan& plan, std::string_view floorId);
const Floor* floorBelow(const Plan& plan, std::string_view floorId);

// Finds a roof section entry by its id. Null when there is none.
RoofSection* findRoofSection(Plan& plan, std::string_view id);
const RoofSection* findRoofSection(const Plan& plan, std::string_view id);

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
Stair* findStair(Floor& floor, std::string_view id);
const Stair* findStair(const Floor& floor, std::string_view id);
LightMarker* findLight(Floor& floor, std::string_view id);
const LightMarker* findLight(const Floor& floor, std::string_view id);

// Says whether any floor, object or roof section entry in the plan already uses this id.
bool idInUse(const Plan& plan, std::string_view id);

// Makes an id that no floor, object or roof section of the plan has: the prefix, a digit, then seven characters
// of kIdAlphabet with at least one letter, for example "i7k3q9xm2", drawn at random and again while in use.
// Removed ids are not remembered: a new id matches one only by chance, about once in 343 billion. When
// kMaxIdDraws tries were all in use, which a working source never gives, it is the prefix and one more than the
// highest number present, counting on while that is in use.
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
// is not part of the format; itemProblems in Items.h reports that. A generator item instead has an empty
// asset, a generator name that is a well formed lower case name (letters a to z, digits and "_", starting with
// a letter, at most 64), parameter ids of the same form, each once and sorted, and finite numbers. Whether the
// generator exists or its values fit its declaration is not part of the format either.
// Floors (format 5): slab thickness from kMinSlabThickness to kMaxSlabThickness, image offset finite.
// Stairs: type a well formed name (registered or not, like opening shapes), width from kMinStairWidth to
// kMaxStairWidth (Stairs.h), rotation from 0 up to 360, material well formed when set. Whether a stair has a
// floor above and fits is not part of the format; stairProblems in Stairs.h reports that.
// Roof settings (the default and each entry): type a well formed name, pitch from 0 to kMaxRoofPitch, overhang
// from 0 to kMaxRoofOverhang, parapet from 0 to kMaxParapet (Roofs.h), direction from 0 up to 360 when set.
// Roof section entries: floor a floor of the plan, anchor finite, material well formed when set.
// Lights (format 7): x and y finite, z from 0 to the floor's ceiling height, room (when set) a room of their
// own floor.
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

// The name used in files for a stair turn: "left" or "right".
std::string_view stairTurnName(StairTurn turn);

// The stair turn for a name used in files. Empty when the name is unknown.
std::optional<StairTurn> stairTurnFromName(std::string_view name);

}
