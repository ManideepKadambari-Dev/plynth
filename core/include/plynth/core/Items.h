#pragma once

#include "Geometry.h"
#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Placed assets (DESIGN.md sections 7.5 and 7.6). Lengths in metres, rotations in degrees, positions in plan
// coordinates (x right, y down). The app and the CLI read items through these functions, so both see the same
// footprint. Blender reads plan.json, and these functions define the conventions it follows.
//
// Turning an item into Blender (milestone 8): Blender x = x, Blender y = -y, Blender z = floor level + z, and
// rotation about Blender's z axis = -rotation (in radians). With the catalog rule that an asset's front faces
// Blender -Y, rotation 0 then faces +y on the plan, as Plan.h says.

// The handle on the plan snaps the rotation to this step unless the user turns freely.
inline constexpr double kItemRotationStep = 15.0;

// The rotate keys and buttons turn by this much.
inline constexpr double kItemQuarterTurn = 90.0;

// Shapes closer than this still count as touching, not overlapping, and a wall item's back may be this far
// from its wall face and still count as flush.
inline constexpr double kItemTouchTolerance = 0.001;

// Two shapes overlap only when the shared area is more than this, in square metres (1 cm by 1 cm).
inline constexpr double kItemOverlapArea = 0.0001;

// A wall item whose back is further than this from its wall face is reported as off its wall.
inline constexpr double kItemOffWallTolerance = 0.01;

// The wall a "Mount on wall" action should use for a point: the wall whose band is nearest the point and within
// radius of it (ties go to the nearer centre line). Empty when none is. The app and the CLI both use it.
Id nearestWallForMount(const Floor& floor, Vec2 point, double radius);

// A rotation in degrees brought into the range from 0 up to 360. Not finite stays as it is.
double normalizeRotation(double degrees);

// The unit direction an item's front faces on the plan: (0, 1) turned clockwise by rotation.
Vec2 itemFront(const Item& item);

// The rectangle an item covers on the plan.
struct ItemFootprint {
    // Corners in the order back right, back left, front left, front right, where left and right are the
    // item's own, as seen from behind it looking toward its front. Positive signedArea. At rotation 0 that is
    // (x - w/2, y - d/2), (x + w/2, y - d/2), (x + w/2, y + d/2), (x - w/2, y + d/2).
    std::array<Vec2, 4> corners;
    Vec2 centre;
    Vec2 front; // unit direction the front faces (itemFront)
};

// Works out an item's footprint from its position, rotation and size.
ItemFootprint itemFootprint(const Item& item);

// The height of the middle of an item above the floor: z plus half its height.
double itemCentreHeight(const Item& item);

// The z that puts the middle of an item of this height at the project's wall item centre (DefaultSizes in
// Project.h), never below 0.
double defaultWallItemZ(const DefaultSizes& defaults, double height);

// Checks the rules every command enforces before it changes an item. In order:
// - InvalidValue when the asset is not a well formed name, a number is not finite, a size is not above zero,
//   or z is below zero. For a generator item: when the asset is not empty, or the generator name or a
//   parameter id is not a well formed lower case name, or a parameter id is used twice. Whether the values fit
//   the generator's declaration is checked by the front ends with checkGeneratorParams (Generators.h).
// - NotFound when room or wall is set and is not a room or wall of the floor.
// - InvalidValue for a wall item whose top (z + height) is above its wall's height (wallHeight), or that is
//   wider than its wall's length. The message gives the numbers.
// Fit inside the room and overlaps are warnings, not refusals: see itemProblems.
Status checkItemPlacement(const Floor& floor, const Item& item);

// Where a wall item hangs on its wall.
struct WallItemSpot {
    bool valid = false;   // false when the item has no wall, its wall is not on the floor or its curve is not valid
    Id wall;
    WallSide side = WallSide::Right; // the face it hangs on: the side of the centre line its position is on
    double offset = 0.0;  // along the wall's true centre line from its from point to the item's centre
    double gap = 0.0;     // distance from the item's back to the wall face; about 0 when flush
};

// Works out where a wall item hangs, from its position and its wall.
WallItemSpot wallItemSpot(const Floor& floor, const Item& item);

// A position and rotation for an item.
struct ItemPose {
    Status status;
    Vec2 position;
    double rotation = 0.0;
    double offset = 0.0; // the offset used, after keeping the item within the wall's length
};

// The pose that hangs an item of this size flush on one face of a wall: its back on the face, facing away from
// the wall, centred at offset along the wall's true centre line. The offset is kept from width / 2 to the
// wall's length minus width / 2 (the middle when the item is wider than the wall). On a curved wall the back
// touches the face at the item's centre. NotFound when the wall is not on the floor, InvalidValue when offset
// or a size is not finite or the wall's curve is not valid.
ItemPose wallItemPose(const Floor& floor, const Wall& wall, WallSide side, double offset, const ItemSize& size);

// How to snap a floor item while it is placed or dragged.
struct ItemSnapOptions {
    double gridSize = 0.0; // 0 or less turns grid snapping off
    double radius = 0.0;   // how close a wall face must be to a footprint side to snap to it; 0 turns that off
};

struct ItemSnap {
    Vec2 position;
    bool toGrid = false;
    std::vector<Id> walls; // the walls the footprint now touches, at most two
};

// Snaps the centre of a floor item that is wanted at a position. In order:
// 1. The centre is rounded to the grid (both coordinates to multiples of gridSize from (0, 0)).
// 2. Each side of the footprint that runs along a wall face (within kInLineAngle of it) and is within radius
//    of it is moved onto the face, from outside the band. The nearest such face wins; a second face is used
//    only when it is not parallel to the first, so an item slides into a corner. A wall snap replaces the grid
//    on the axis it moves.
// The item's rotation and size are used as they are; its own position is ignored.
ItemSnap snapItemPosition(const Floor& floor, const Item& item, Vec2 wanted, const ItemSnapOptions& options);

// Something wrong with a placed item that does not stop the plan from being saved or built.
enum class ItemProblemKind {
    NoRoom,          // "no_room": its centre is in no room
    OutsideRoom,     // "outside_room": part of its footprint is outside its room's floor area
    OverlapsItem,    // "overlaps_item": its box overlaps another item's box (footprints and heights), leaving
                     // out the item it stands on and an underlay under a floor item (Stacking.h)
    OverlapsWall,    // "overlaps_wall": its footprint overlaps a wall band (a wall item's own wall is skipped)
    OverlapsOpening, // "overlaps_opening": a wall item's box on the wall face overlaps an opening on that wall
    OffWall,         // "off_wall": a wall item is further than kItemOffWallTolerance from its wall face
    TooTall,         // "too_tall": a floor item reaches above the ceiling, or a wall item above its wall
    MissingAsset,    // "missing_asset": the asset is not in the library
    SizeChanged,     // "size_changed": the library's size for the asset differs from the stored one
    MissingGenerator,     // "missing_generator": a generator item's generator is not in the library
    GeneratorUnavailable, // "generator_unavailable": its generator needs an add-on that is not enabled
    GeneratorChanged,     // "generator_changed": its values no longer fit the generator's declaration, or the
                          // size worked out from them differs from the stored one
};

// The stable lower case name of a problem kind, used by the CLI as a warning code.
std::string_view itemProblemName(ItemProblemKind kind);

struct ItemProblem {
    Id item;
    ItemProblemKind kind = ItemProblemKind::NoRoom;
    Id other;            // the other item, wall or opening for an overlap; empty otherwise
    std::string message; // one plain sentence, for example "Sofa i3 overlaps table i4."
};

// Says what the library knows about an asset: its measured size, or empty when the asset is missing from the
// library. Made from the catalog with assetLookupFor (catalog/ItemAssets.h).
using AssetLookup = std::function<std::optional<ItemSize>(std::string_view asset)>;

// What the library says about the generator of a generator item.
struct GeneratorItemState {
    bool available = true;            // false when an add-on it needs is known to be off
    std::string unavailableReason;    // one plain sentence when not available, naming the add-ons
    bool valuesFit = true;            // the stored values pass checkGeneratorParams
    std::optional<ItemSize> size;     // generatorSize of the stored values; empty when they do not fit
};

// Says what the library knows about a generator: its state, or empty when the generator is not in the library.
// Made with generatorItemLookupFor (Generators.h).
using GeneratorItemLookup = std::function<std::optional<GeneratorItemState>(const GeneratorUse& use)>;

// The problems of one item as it would be on the floor: an item with the id of one already there stands in
// for it, so a command or a drag preview can check the result before it runs. Overlaps are checked against the
// floor's other items. MissingAsset and SizeChanged are checked only when lookup is given, and only for asset
// items. MissingGenerator, GeneratorUnavailable and GeneratorChanged are checked only when generators is given,
// and only for generator items. Each overlapping item, wall or opening gives its own entry. Order: as
// ItemProblemKind, then by the other id.
// The first form passes no generator lookup.
std::vector<ItemProblem> checkItem(const Floor& floor, const Item& item, const AssetLookup& lookup = {});
std::vector<ItemProblem> checkItem(const Floor& floor, const Item& item, const AssetLookup& lookup,
                                   const GeneratorItemLookup& generators);

// The problems of every item on the floor, in the floor's item order (checkItem for each). The app draws those
// items in Signal amber and the CLI lists them.
std::vector<ItemProblem> itemProblems(const Floor& floor, const AssetLookup& lookup = {});
std::vector<ItemProblem> itemProblems(const Floor& floor, const AssetLookup& lookup,
                                      const GeneratorItemLookup& generators);

}
