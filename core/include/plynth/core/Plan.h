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
// Plan format 8.
inline constexpr std::string_view kCladdingIdPrefix = "cz";
inline constexpr std::string_view kDeckIdPrefix = "dk";
inline constexpr std::string_view kRailingIdPrefix = "rl";
inline constexpr std::string_view kCanopyIdPrefix = "cn";
inline constexpr std::string_view kDormerIdPrefix = "dm";
inline constexpr std::string_view kLightStripIdPrefix = "ls";
inline constexpr std::string_view kBoundaryIdPrefix = "sb";
inline constexpr std::string_view kGateIdPrefix = "g";
inline constexpr std::string_view kSiteSurfaceIdPrefix = "ss";

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

// A position on the plan in metres, for outlines and lines drawn on it (plan format 8). In plan.json
// {"x": 1.0, "y": 2.0}.
struct PlanXY {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const PlanXY&) const = default;
};

// The two sides of a wall, as seen on the plan looking from its from point toward its to point. Right is the
// direction (-dy, dx), where (dx, dy) runs from "from" to "to"; with y pointing down that is the right hand side
// on screen.
enum class WallSide { Left, Right };

inline constexpr double kDefaultCladdingDepth = 0.02;

// A zone of cladding on one face of a wall (plan format 8): wood battens, metal or stone panels over part of the
// face, not the whole wall. start and end are measured along the wall like an opening's offset, bottom and top
// are heights above the floor. depth is how far the cladding stands out from the face. material is a catalog id
// and is required. Cladding is cut where an opening of the wall passes through it.
struct CladdingZone {
    Id id;
    WallSide side = WallSide::Right;
    double start = 0.0;
    double end = 0.0;
    double bottom = 0.0;
    double top = 0.0;
    double depth = kDefaultCladdingDepth;
    std::string material;

    bool operator==(const CladdingZone&) const = default;
};

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
    std::vector<CladdingZone> cladding; // plan format 8

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
    // The kind of fill Plynth builds when no catalog door or window is fitted (plan format 8): an entry of the
    // opening fill registry, for example "sliding" for a door or "glass_wall" for a window that runs floor to
    // ceiling. Empty: the type's own fill, a hinged door or a framed window. Kept when the fill is not registered.
    std::optional<std::string> fill;

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
    // The item of the same floor this one stands on (plan format 8), such as a sofa on a rug or a lamp on a table.
    // z is then the top of that item (its z plus its height), and the item commands keep it so when the lower item
    // moves or changes size. Empty: it stands on the floor, or hangs on its wall. Stacking.h has the rules.
    std::optional<Id> on;

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
    // Plan format 8. rise empty: the stair climbs to the next floor as above. Set: outside steps that climb this
    // many metres from the floor's level, to a deck, a porch or a door sill, with no floor above needed and no
    // hole cut. cheeks adds a solid side wall along both sides of the flight. railing builds its railings.
    std::optional<double> rise;
    bool cheeks = false;
    bool railing = true;

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

// How a roof section is built (DESIGN.md section 7.9). type names an entry of the roof type registry
// (Roofs.h): "flat" (with a parapet), "gable", "hip", "shed" or "sawtooth". pitch is in degrees above
// horizontal, overhang is how far a pitched roof (or a flat one without a parapet) reaches past the outer wall
// faces, parapet is the height of the low wall round a flat roof (0 for none). direction is in degrees from 0
// up to 360, clockwise on the plan from +x: the ridge of a gable or sawtooth roof runs along it, and a shed
// roof falls toward it. Empty direction means automatic (see Roofs.h). Each type says which fields it uses.
// Plan format 8 adds the fields of the two pitch and curved types (Roofs.h): secondPitch is the upper pitch of a
// gambrel or mansard and the eaves pitch of a bonnet, breakRatio the share of the run the lower slope covers
// (0.1 to 0.9), ridgeOffset moves the ridge of a saltbox or the valley of a butterfly across the section as a
// share of its width (-0.45 to 0.45, 0 is the middle), and rise is the height in metres of a barrel, dome or bow
// roof and of the glazed band of a clerestory.
struct RoofSettings {
    std::string type = "flat";
    double pitch = 30.0;
    double overhang = 0.3;
    double parapet = 0.6;
    std::optional<double> direction;
    double secondPitch = 15.0;
    double breakRatio = 0.5;
    double ridgeOffset = 0.0;
    double rise = 1.0;

    bool operator==(const RoofSettings&) const = default;
};

// A raised open floor outside the walls: a porch, deck, veranda or terrace (plan format 8). outline is a simple
// polygon on the plan, at least three points, the first not repeated. top is the height of the walking surface
// above the floor's level, thickness how deep the deck is under it. skirt closes the sides from the deck down to
// the ground (the floor's level). Materials empty: the project's deck default (MaterialDefaults.h).
struct Deck {
    Id id;
    std::vector<PlanXY> outline;
    double top = 0.6;
    double thickness = 0.15;
    std::optional<std::string> material;
    bool skirt = true;
    std::optional<std::string> skirtMaterial;

    bool operator==(const Deck&) const = default;
};

// A railing along a line drawn on the plan (plan format 8): along a balcony, terrace, deck or porch edge. points
// is the line, at least two points; closed joins the last back to the first. It stands on the top of its host
// deck when host is set, otherwise base metres above the floor's level. style names an entry of the railing style
// registry (Railings.h), for example "posts_rails" or "glass". Materials empty: the project's railing and glass
// defaults.
struct Railing {
    Id id;
    std::vector<PlanXY> points;
    bool closed = false;
    std::optional<Id> host;
    double base = 0.0;
    double height = 1.0;
    std::string style = "posts_rails";
    std::optional<std::string> material;
    std::optional<std::string> glassMaterial;

    bool operator==(const Railing&) const = default;
};

// A roof over an open area with no walls under it: a porch roof, veranda, carport or a deep slab canopy (plan
// format 8). outline is drawn on the plan like a deck's. height is the underside above the floor's level.
// roof is built over the outline by its roof type, as a roof section is; a flat type with parapet 0 makes a
// plain slab with a fascia band thickness deep. Columns are generator items placed under it. Materials empty:
// the project's roof or flat_roof default for the covering, the neutral soffit and fascia looks otherwise.
struct Canopy {
    Id id;
    std::vector<PlanXY> outline;
    double height = 2.7;
    double thickness = 0.25;
    RoofSettings roof{"flat", 30.0, 0.0, 0.0, std::nullopt};
    std::optional<std::string> material;
    std::optional<std::string> undersideMaterial;
    std::optional<std::string> fasciaMaterial;

    bool operator==(const Canopy&) const = default;
};

// A dormer in a pitched roof of this floor's roof sections (plan format 8): a small box with its own roof and a
// window in its front, cut cleanly into the main roof. (x, y) is the middle of its front on the plan; the front
// faces down the slope of the roof plane it stands on. type is "gable" or "shed", pitch its own roof's pitch,
// width across the front, depth from the front back into the main roof. The window is windowWidth by
// windowHeight, sill windowSill above the dormer's floor line. Materials empty: wall_outside for the cheeks and
// front, the roof default for its roof.
struct Dormer {
    Id id;
    double x = 0.0;
    double y = 0.0;
    std::string type = "gable";
    double width = 1.6;
    double depth = 1.5;
    double pitch = 30.0;
    double windowWidth = 0.9;
    double windowHeight = 1.0;
    double windowSill = 0.2;
    std::optional<std::string> material;
    std::optional<std::string> roofMaterial;

    bool operator==(const Dormer&) const = default;
};

// A strip of light along a line (plan format 8): a cove, a soffit or an overhang edge, a slab edge, or the steps
// of a stair. points is the line, at least two points, z the height above the floor's level. For kind Step,
// stair names a stair of this floor and the strip runs under the nose of every step; points is then empty.
// power is in watts per metre.
struct LightStrip {
    Id id;
    LightStripKind kind = LightStripKind::Cove;
    std::vector<PlanXY> points;
    double z = 2.9;
    std::optional<Id> stair;
    double power = 10.0;

    bool operator==(const LightStrip&) const = default;
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
    // Plan format 8.
    std::vector<Deck> decks;
    std::vector<Railing> railings;
    std::vector<Canopy> canopies;
    std::vector<Dormer> dormers;
    std::vector<LightStrip> lightStrips;

    bool operator==(const Floor&) const = default;
};

// The kinds of site boundary. Names in files: "fence" (posts and rails in a railing style), "wall" (a solid
// boundary wall).
enum class BoundaryKind { Fence, Wall };

// A gate in a site boundary. offset is measured along the boundary's line from its first point to the gate's
// centre, width along the same line.
struct Gate {
    Id id;
    double offset = 0.0;
    double width = 1.0;
    bool sliding = false;
    std::optional<std::string> material;

    bool operator==(const Gate&) const = default;
};

// A fence or boundary wall drawn round the site. Its line is drawn like a railing's. style is a railing style
// for a fence and is not used for a wall. Material empty: the project's fence or boundary_wall default.
struct SiteBoundary {
    Id id;
    BoundaryKind kind = BoundaryKind::Fence;
    std::vector<PlanXY> points;
    bool closed = false;
    double height = 1.2;
    double thickness = 0.1;
    std::string style = "posts_rails";
    std::optional<std::string> material;
    std::vector<Gate> gates;

    bool operator==(const SiteBoundary&) const = default;
};

// The kinds of flat site surface. Names in files: "path", "driveway", "paving", "lawn".
enum class SiteSurfaceKind { Path, Driveway, Paving, Lawn };

// A flat surface laid on the ground: an outline drawn like a deck's, thickness above the ground. Material empty:
// the project's paving default, or ground for a lawn.
struct SiteSurface {
    Id id;
    SiteSurfaceKind kind = SiteSurfaceKind::Path;
    std::vector<PlanXY> outline;
    double thickness = 0.05;
    std::optional<std::string> material;

    bool operator==(const SiteSurface&) const = default;
};

// The ground plane round the building: a rectangle reaching margin metres past everything built, cut round the
// floors below ground level. Material empty: the project's ground default.
struct SiteGround {
    bool enabled = false;
    double margin = 10.0;
    std::optional<std::string> material;

    bool operator==(const SiteGround&) const = default;
};

// Everything round the building (plan format 8), on the plan of the ground floor and at ground level.
struct Site {
    SiteGround ground;
    std::vector<SiteBoundary> boundaries;
    std::vector<SiteSurface> surfaces;

    bool operator==(const Site&) const = default;
};

// The plinth or foundation band at the bottom of the outer walls of the ground floor (plan format 8): height
// above ground level, stepOut how far it stands out from the outer wall faces. Material empty: the project's
// foundation default.
struct Foundation {
    bool enabled = false;
    double height = 0.45;
    double stepOut = 0.03;
    std::optional<std::string> material;

    bool operator==(const Foundation&) const = default;
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
// groundFloor (plan format 8) is the floor at ground level; floors below it in plan order lie below the ground,
// each one floor rise lower than the floor above it (Storeys.h). Empty: the lowest floor is at ground level.
struct Plan {
    Scale scale;
    std::vector<Floor> floors;
    RoofSettings roof;
    std::vector<RoofSection> roofSections;
    std::optional<Id> groundFloor;
    Site site;
    Foundation foundation;

    bool operator==(const Plan&) const = default;
};

// Makes a plan with one empty floor "f1" named "Floor 1", no image and no scale.
Plan makeEmptyPlan();

// Finds a floor by its id. Null when there is none.
Floor* findFloor(Plan& plan, std::string_view floorId);
const Floor* findFloor(const Plan& plan, std::string_view floorId);

// Finds the floor that holds the point, wall, room, opening, item, stair or light with this id, and from plan
// format 8 the cladding zone, deck, railing, canopy, dormer or light strip. Null when there is none.
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
// The id of the first wall in plan order, on the first floor that has one. Empty when the plan has no wall.
Id firstWallId(const Plan& plan);
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

// Says whether any floor, object, roof section entry or site object in the plan already uses this id.
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
// Format 8: every number finite. Cladding: start below end, bottom below top, both from 0, depth from 0.005 to
// 0.2, material a well formed name. Opening fill well formed when set. Item on: an item of the same floor, not
// the item itself, no loop of items standing on each other, and never set together with wall. Stair rise from
// 0.1 to 6 when set. Roof settings: secondPitch from 0 to kMaxRoofPitch, breakRatio from 0.1 to 0.9,
// ridgeOffset from -0.45 to 0.45, rise from 0.1 to 10. Decks, canopies and site surfaces: an outline of at
// least three points with an area above 0.01 square metres; deck top from 0 to 10 and thickness from 0.05 to 1;
// canopy height from 1 to 20 and thickness from 0.05 to 1. Railings and boundaries: at least two points,
// height from 0.3 to 3 (boundaries 0.3 to 4), thickness from 0.02 to 1, style well formed; a railing host is a
// deck of the same floor and its base from 0 to 20; a site surface thickness from 0 to 0.5; a gate's centre lies
// on its boundary's line (offset from 0 to the line's length) with width from 0.5 to 6. Dormers: type well
// formed, width and depth from 0.5 to 6, pitch from kMinPitchedRoofPitch to kMaxRoofPitch, the window from
// 0.2 m on each side and inside the front. Light strips: at least two points except kind step, which needs a
// stair of the same floor; z from 0 to the floor's ceiling height; power from 0 to 200. groundFloor a floor of
// the plan when set. Site ground margin from 0 to 500; foundation height from 0.05 to 2 and stepOut from 0 to
// 0.3. Whether an outline crosses itself, a railing or deck fits, a dormer lies on a pitched roof, or a fill,
// railing style or roof type is registered is not part of the format; the build reports it.
Status checkPlan(const Plan& plan);

// Find one plan format 8 object on a floor by its id. Null when there is none.
Deck* findDeck(Floor& floor, std::string_view id);
const Deck* findDeck(const Floor& floor, std::string_view id);
Railing* findRailing(Floor& floor, std::string_view id);
const Railing* findRailing(const Floor& floor, std::string_view id);
Canopy* findCanopy(Floor& floor, std::string_view id);
const Canopy* findCanopy(const Floor& floor, std::string_view id);
Dormer* findDormer(Floor& floor, std::string_view id);
const Dormer* findDormer(const Floor& floor, std::string_view id);
LightStrip* findLightStrip(Floor& floor, std::string_view id);
const LightStrip* findLightStrip(const Floor& floor, std::string_view id);

// Finds a cladding zone by its id, with the wall that holds it. Null when there is none.
CladdingZone* findCladding(Floor& floor, std::string_view id, Wall** wall = nullptr);
const CladdingZone* findCladding(const Floor& floor, std::string_view id, const Wall** wall = nullptr);

// Find a site boundary, a gate (with its boundary) or a site surface by its id. Null when there is none.
SiteBoundary* findBoundary(Plan& plan, std::string_view id);
const SiteBoundary* findBoundary(const Plan& plan, std::string_view id);
Gate* findGate(Plan& plan, std::string_view id, SiteBoundary** boundary = nullptr);
const Gate* findGate(const Plan& plan, std::string_view id, const SiteBoundary** boundary = nullptr);
SiteSurface* findSiteSurface(Plan& plan, std::string_view id);
const SiteSurface* findSiteSurface(const Plan& plan, std::string_view id);

// The floor at ground level: Plan::groundFloor when set, otherwise the lowest floor.
const Floor& groundLevelFloor(const Plan& plan);

// The names used in files for the format 8 kinds, and the kind for a name. Empty for an unknown name.
std::string_view boundaryKindName(BoundaryKind kind);
std::optional<BoundaryKind> boundaryKindFromName(std::string_view name);
std::string_view siteSurfaceKindName(SiteSurfaceKind kind);
std::optional<SiteSurfaceKind> siteSurfaceKindFromName(std::string_view name);

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
