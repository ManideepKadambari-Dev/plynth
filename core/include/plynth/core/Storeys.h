#pragma once

#include "BuildGeometry.h"
#include "Geometry.h"
#include "Plan.h"

#include <cstddef>
#include <vector>

namespace plynth {

// How floors stack (DESIGN.md sections 7.7, 7.8 and 7.9). Worked out in core from the walls and rooms of each
// floor, so the plan view, the CLI and the build agree.
//
// A floor's footprint is everything it covers: the union of its walls as built (wallOutline, never wallBand)
// and, for each closed room, the whole area inside its centre lines (RoomShape::rings: the outer ring less the
// islands). Never RoomShape::floorArea: that is cut with the square bands, which reach past a mitred corner
// and would leave pinholes beside the walls. So the footprint's boundary lies on that of the wall solids
// (wallSolids, all heights together) within 1e-6, and its only holes are the enclosed areas that are no room,
// such as an open courtyard.
//
// Every floor i has a slab above its ceiling, from level(i) + ceilingHeight(i) to level(i + 1) =
// level(i) + ceilingHeight(i) + slabThickness(i) (for the top floor, up to where its roof sits). The slab covers
// footprint(i) together with footprint(i + 1), minus the holes of the stairs that climb from floor i. Where
// footprint(i + 1) reaches past footprint(i), the slab carries the upper floor and its underside shows: that is
// the rule that "closes the underside of an overhanging upper floor". Where footprint(i) is not covered by
// footprint(i + 1), the slab carries a roof section (Roofs.h) wherever that part is wide enough for one
// (kMinRoofSectionWidth). The narrow rest is a ledge, where the slab's top face is built so no gap is left.
// Everywhere else the top is under the floor above or under a roof and is not built. The lowest floor has no
// slab below it.

// The narrowest part of exposedArea that gets a roof, in metres. A hairline mismatch between two floors is
// narrower, and so is anything that could not hold a parapet (kParapetThickness).
inline constexpr double kMinRoofSectionWidth = 0.2;

// How much narrower than kMinRoofSectionWidth a part may be through rounding and still get a roof.
inline constexpr double kRoofWidthTolerance = 1e-4;

// How near the uncut slab's boundary an edge must lie to be SlabSide::Edge, in metres.
inline constexpr double kSlabSideTolerance = 1e-5;

// A polygon with holes in plan coordinates. outline has a positive signedArea, holes a negative one, the first
// point not repeated.
struct PlanArea {
    std::vector<Vec2> outline;
    std::vector<std::vector<Vec2>> holes;

    bool operator==(const PlanArea&) const = default;
};

// The area of a polygon with holes, square metres.
double planAreaSize(const PlanArea& area);

// Everything a floor covers, as above. Pieces in a stable order (by their lowest then leftmost point).
std::vector<PlanArea> floorFootprint(const Floor& floor);

// The height from a floor's level to the next floor's: its ceiling height plus its slab thickness.
double floorRise(const Floor& floor);

// The part of floor i's footprint with no footprint of floor i + 1 above it; the whole footprint for the top
// floor. Narrow parts included, in the order of floorFootprint's rule.
std::vector<PlanArea> exposedArea(const Plan& plan, std::size_t floorIndex);

// The roof sections of floor i: exposedArea without the parts narrower than kMinRoofSectionWidth (less
// kRoofWidthTolerance). That is exposedArea shrunk by half that width, grown back by the same with mitred
// corners and kept inside exposedArea, so a hairline strip is dropped also when it joins a wide part, and a
// narrow neck parts two sections. The rule is the same for the top floor. Each piece is one section, in the
// order of floorFootprint's rule, without jogs or slivers left by rounding.
std::vector<PlanArea> roofSectionAreas(const Plan& plan, std::size_t floorIndex);

// The part of floor i + 1's footprint that is not above floor i's footprint. Empty for the top floor.
std::vector<PlanArea> overhangArea(const Plan& plan, std::size_t floorIndex);

// Works out the slab above floor i (see the top of this header) at levels (floorElevations), in world
// coordinates:
// - polygons is the slab's area with the stair holes cut
// - undersides is overhangArea minus the same holes
// - tops is exposedArea minus roofSectionAreas and minus the same holes: the ledges
// - sides says what each ring edge of polygons is. Edge when it lies on the boundary of the slab's area before
//   the holes were cut (within kSlabSideTolerance), whether that ring is an outline or a hole (a courtyard).
//   Well otherwise: a cut made that edge, also where a well reaches the boundary and joins the outline. An
//   edge that would be both has a point where it changes.
SlabBuild slabBuild(const Plan& plan, std::size_t floorIndex, const std::vector<double>& levels);

}
