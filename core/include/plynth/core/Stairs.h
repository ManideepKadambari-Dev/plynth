#pragma once

#include "Geometry.h"
#include "Plan.h"
#include "Status.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Stairs (DESIGN.md section 7.8). Stair types are registry entries: a built-in type is one file in
// core/src/stairs/types/ with a StairTypeRegistrar at namespace scope, so adding one never means editing
// existing code. A type turns the rise, the width and the turn into steps in the stair's own frame; core places
// them on the plan, and the Blender build only makes a closed mesh per step.
//
// The stair's own frame: u across the stair to the right of the climb direction, v forward along the climb,
// z up from the lower floor's level, all in metres, with (0, 0) at the middle of the bottom step's front edge.
// Plan position = (stair.x, stair.y) + v * climb + u * rightOf(climb), where climb is (0, 1) turned clockwise
// by stair.rotation (as itemFront in Items.h). A turn to the right goes toward +u.
//
// Steps: the rise is the lower floor's ceiling height plus its slab thickness (the difference of the two floor
// levels). riserCount = max(2, ceil(rise / kMaxRiser)), every riser rise / riserCount. There are riserCount - 1
// treads; the last riser steps onto the upper floor. A landing takes the place of one tread. Steps and
// landings are solid from z = 0 up to their top, except on the spiral, whose steps are kSpiralTreadThickness
// thick and stand on a centre column.
//
// The built-in types (sort key in brackets):
//   straight (10)  one flight of treads kStairGoing deep, v from 0 to (riserCount - 1) * kStairGoing
//   l_shape  (20)  a first flight of floor(treads / 2) treads, a square landing width by width, then the rest
//                  turned 90 degrees to the turn side
//   u_shape  (30)  a first flight of floor(treads / 2) treads, a landing 2 * width + kUStairGap across and width
//                  deep, then the rest coming back beside the first flight, kUStairGap away on the turn side
//   spiral   (40)  wedge steps round a column of radius kSpiralColumnRadius whose centre is
//                  kSpiralColumnRadius + width / 2 to the turn side of (0, 0); each tread turns by
//                  kStairGoing / (kSpiralColumnRadius + width / 2) radians, winding toward the turn side
// Arcs are cut with arcSegmentCount.

inline constexpr std::string_view kStraightStairId = "straight";
inline constexpr std::string_view kLShapeStairId = "l_shape";
inline constexpr std::string_view kUShapeStairId = "u_shape";
inline constexpr std::string_view kSpiralStairId = "spiral";

inline constexpr double kMaxRiser = 0.19;           // metres
inline constexpr double kStairGoing = 0.27;         // tread depth, metres (on the walk line for a spiral)
inline constexpr double kUStairGap = 0.1;           // between the two flights of a U shaped stair
inline constexpr double kSpiralColumnRadius = 0.1;
inline constexpr double kSpiralTreadThickness = 0.05;
inline constexpr double kMinStairWidth = 0.6;
inline constexpr double kMaxStairWidth = 3.0;

// What a step is, for its material, its collision and how the plan draws it.
enum class StairPart {
    Step,    // "step"
    Landing, // "landing"
    Column,  // "column": the centre post of a spiral stair
};

std::string_view stairPartName(StairPart part);
std::optional<StairPart> stairPartFromName(std::string_view name);

// One solid piece of a stair: a convex outline raised from bottom to top. The outline is a simple polygon with
// positive signedArea in the frame it is given in, the first point not repeated. bottom and top are metres
// above the lower floor's level.
struct StairStep {
    StairPart part = StairPart::Step;
    std::vector<Vec2> outline;
    double bottom = 0.0;
    double top = 0.0;

    bool operator==(const StairStep&) const = default;
};

// What a stair type needs to know.
struct StairInput {
    double rise = 0.0;     // above zero
    double width = 0.0;    // from kMinStairWidth to kMaxStairWidth
    StairTurn turn = StairTurn::Right;
    int riserCount = 0;    // worked out from rise as the header says
    double riser = 0.0;    // rise / riserCount
};

// A stair type's answer, in the stair's own frame (u, v).
struct StairShape {
    std::vector<StairStep> steps; // bottom to top, then the column of a spiral
    std::vector<Vec2> walkLine;   // from (0, 0) to where the climb reaches the upper floor, for the plan arrow
};

class StairType {
public:
    virtual ~StairType() = default;

    // Stable id stored in plan.json, for example "u_shape". Lower case letters, digits and "_".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "U shape".
    virtual std::string_view name() const = 0;
    // Types are listed by this key, then by id.
    virtual int sortKey() const = 0;
    // True when the type uses the turn side (L, U and spiral); the app hides the field otherwise.
    virtual bool usesTurn() const = 0;
    // The steps and the walk line for this input.
    virtual StairShape shape(const StairInput& input) const = 0;
};

// Adds a stair type to the registry. Returns false and drops it when its id is taken or not a well formed lower
// case name.
bool registerStairType(std::unique_ptr<StairType> type);

// Registers a stair type while the program starts, for example
// const StairTypeRegistrar registrar(std::make_unique<SpiralStair>());
struct StairTypeRegistrar {
    explicit StairTypeRegistrar(std::unique_ptr<StairType> type);
};

// The stair type with this id. Null when there is none.
const StairType* findStairType(std::string_view id);

// Every registered stair type, sorted by sortKey and then id.
std::vector<const StairType*> allStairTypes();

// A stair as it stands on the plan.
struct StairLayout {
    // InvalidValue "Stair s2 has no floor above it." when its floor is the top one, InvalidValue naming the
    // known ids when its type is not registered, InvalidValue for a width outside the range. The rest is empty
    // then.
    Status status;
    Id stair;
    Id fromFloor;
    Id toFloor;
    double rise = 0.0;
    int riserCount = 0;
    double riser = 0.0;
    double going = kStairGoing;
    std::vector<StairStep> steps; // plan coordinates; bottom and top above the lower floor's level
    // The union of the step outlines on the plan, positive signedArea. It is the hole cut through the slab
    // and the upper floor's room floors, and the locked outline the upper floor shows.
    std::vector<Vec2> footprint;
    std::vector<Vec2> walkLine;   // plan coordinates, bottom to top
    Vec2 arrival;                 // the walk line's last point: where the stair reaches the upper floor
    double length = 0.0;          // the walk line's length, metres
};

// Works out a stair on its floor. The floor must be in the plan and hold the stair.
StairLayout stairLayout(const Plan& plan, const Floor& floor, const Stair& stair);

// The layouts of every stair on floors below that reach this floor (the floor right below, in plan order), in
// that floor's stair order. Only those whose status is ok.
std::vector<StairLayout> stairsArrivingAt(const Plan& plan, std::string_view floorId);

// The same layout for a stair that is not in the plan yet, for the live preview while the user drags the arrow
// on a floor. The stair's id may be empty.
StairLayout previewStairLayout(const Plan& plan, const Floor& floor, const Stair& stair);

// Something wrong with a stair that the build reports. Codes and severities:
//   stair_no_floor_above (error, not built), stair_type_unknown (error, not built),
//   stair_outside_floor (warning: part of the footprint lies outside the floor's footprint, Storeys.h),
//   stair_overlaps_wall (warning: the footprint overlaps a wall band of its floor or of the floor above; the
//   message names the wall and floor), stair_arrival_outside_room (warning: the arrival point is in no room of
//   the floor above).
struct StairProblem {
    Id stair;
    std::string code;
    bool error = false;
    std::string message; // one plain sentence
    Id wall;             // for stair_overlaps_wall, the wall it overlaps

    bool operator==(const StairProblem&) const = default;
};

// Every problem of every stair on a floor, in stair order.
std::vector<StairProblem> stairProblems(const Plan& plan, const Floor& floor);

// Checks the values a stair command would set: type registered, width in range, position finite, rotation
// from 0 up to 360, material well formed when set. InvalidValue with a plain sentence.
Status checkStair(const Stair& stair);

}
