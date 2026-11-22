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
// A floor's footprint is everything it covers: the union of its wall bands (wallBand) and the floor areas of
// its closed rooms (roomShape). An open courtyard that is not a room is not part of it.
//
// Every floor i has a slab above its ceiling, from level(i) + ceilingHeight(i) to level(i + 1) =
// level(i) + ceilingHeight(i) + slabThickness(i) (for the top floor, up to where its roof sits). The slab covers
// footprint(i) together with footprint(i + 1), minus the holes of the stairs that climb from floor i. Where
// footprint(i + 1) reaches past footprint(i), the slab carries the upper floor and its underside shows: that is
// the rule that "closes the underside of an overhanging upper floor". Where footprint(i) is not covered by
// footprint(i + 1), the slab carries a roof section (Roofs.h). The slab's top face is never built: it is always
// under the floor above or under a roof. The lowest floor has no slab below it.

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
// floor. Each piece is one roof section, in the order of floorFootprint's rule.
std::vector<PlanArea> exposedArea(const Plan& plan, std::size_t floorIndex);

// The part of floor i + 1's footprint that is not above floor i's footprint. Empty for the top floor.
std::vector<PlanArea> overhangArea(const Plan& plan, std::size_t floorIndex);

// Works out the slab above floor i (see the top of this header) at levels (floorElevations): polygons is the
// slab's area with the stair holes, undersides is overhangArea minus the same holes, in world coordinates.
SlabBuild slabBuild(const Plan& plan, std::size_t floorIndex, const std::vector<double>& levels);

}
