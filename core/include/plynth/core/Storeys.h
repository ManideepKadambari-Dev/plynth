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
// (wallSolids, all heights together) within kSlabSideTolerance, and its only holes are the enclosed areas that
// are no room, such as an open courtyard.
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
//
// Every area these functions make (footprints, exposed and overhang areas, sections, polygons, undersides, tops
// and the holes in all of them) is cleaned of rounding noise right after each clip. Nothing else is cleaned by
// width but a roof's finished eaves, once (Roofs.h): never the half plane clips inside a roof type, where the
// loss would add up from clip to clip, and never collision.
// First by width, whatever the length or area, with w = kRingMergeDistance and mitred offsets (limit 10):
// - What is narrower than w outside the area is filled (a slit, a hairline hole, a gap between two pieces, also
//   a piece inside another's hole): grow each piece by w / 2, unite all the grown pieces, so near pieces join,
//   only then shrink each united piece by w, and unite that with the area.
// - What is narrower than w inside it is left out (a needle, a hairline ring, a neck between wide parts): shrink
//   each piece of the filled area by w / 2, grow each by w, and intersect their union with the filled area.
//   The union and the intersection are with the unmoved area, so a wide part keeps its exact corners.
// Outlines and holes are told by their turning: a ring that turns left is an outline, one that turns right is a
// hole, and a hole belongs to the smallest outline that holds most of its points. A hole no outline holds is
// noise when less than kMinPlanAreaSize is left of it shrunk by w / 2, and a broken outline otherwise
// (outline_failed, BuildJob.h).
// Then ring by ring:
// - A point nearer than kRingMergeDistance to the last point kept before it in the ring is dropped (the last
//   point against the first too). The kept points never move, so drops in a row do not add up.
// - A spike's tip is dropped: a point p whose neighbour on one side lies within kRingMergeDistance of the
//   segment from p to its neighbour on the other side. A point in a straight line between its neighbours stays,
//   since a junction there is a real point.
// - Both repeat until nothing changes, also below three points. A ring left with fewer than three points or an
//   area under kMinPlanAreaSize is dropped, a hole as much as an outline, and so is a piece with less area
//   than that (planAreaSize). A dropped outline takes its holes along, a dropped hole is filled.
// - A ring that comes within kRingMergeDistance of itself is cut there, and each loop keeps its turning.
// - Then the rings of one piece are checked together: when two of them cross, the piece is united again, read
//   by turning, cut and cleaned again, up to three times. One whose rings still cross deeper than 1e-7 is a
//   broken outline. Rings that only touch within rounding stay as they are. A hole is never bridged.
// So no piece or hole is narrower than kRingMergeDistance all along, no ring edge is shorter, and a boundary
// that stays lies within kSlabSideTolerance of the area as clipped. Where a neck or a slit was cut a stub no
// longer than a few w may be narrower. A thinner mismatch gets no roof, ledge, underside, hole or side face of
// its own.
// Areas add up but for that noise: the slab's polygons against the sections, the tops and the footprint above
// (stair holes taken off), and the undersides against the overhang less the holes, each within
// 2 * kRingMergeDistance times the perimeter of every ring of the two footprints and the holes, plus
// kMinPlanAreaSize for each ring dropped for its area.

// The narrowest part of exposedArea that gets a roof, in metres. A hairline mismatch between two floors is
// narrower, and so is anything that could not hold a parapet (kParapetThickness).
inline constexpr double kMinRoofSectionWidth = 0.2;

// How much narrower than kMinRoofSectionWidth a part may be through rounding and still get a roof.
inline constexpr double kRoofWidthTolerance = 1e-4;

// Points of a ring nearer than this are one point, and nothing narrower is kept, in metres. A little over the
// diagonal of a float32 step (1.08e-5) at up to 128 m from the origin, so no edge or face collapses in a mesh.
inline constexpr double kRingMergeDistance = 1.2e-5;

// A ring with less area than this, in square metres, is rounding noise.
inline constexpr double kMinPlanAreaSize = 1e-6;

// How near the uncut slab's boundary an edge must lie to be SlabSide::Edge, and the footprint's to the wall
// solids', in metres. Always more than kRingMergeDistance, so cleaning never turns an outside band into a well.
inline constexpr double kSlabSideTolerance = 2e-5;

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
//   the holes were cut, whether that ring is an outline or a hole (a courtyard). Well otherwise: a cut made
//   that edge, also where a well reaches the boundary and joins the outline. An edge is judged in pieces, split
//   at every point of that boundary within kSlabSideTolerance of it. A piece is Edge when both its ends and its
//   middle are within kSlabSideTolerance of the boundary (to the nearest segment, its ends included), whatever
//   that segment's direction. The middle is needed: a well's side can run from boundary to boundary. An edge
//   that would be both has a point where it changes.
SlabBuild slabBuild(const Plan& plan, std::size_t floorIndex, const std::vector<double>& levels);

}
