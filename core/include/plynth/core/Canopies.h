#pragma once

#include "BuildParts.h"
#include "Plan.h"
#include "Roofs.h"
#include "Status.h"

namespace plynth {

// Canopies and overhanging upper floors (issues 16, 20 and 31, plan format 8).
//
// Canopies: the part maker "canopies" (order 40, kind "canopy", group "surfaces"),
// core/src/parts/makers/CanopyParts.cpp. A canopy is a slab from its height above the floor's level up to the
// roof's base, soffit below and fascia round its sides, with its outline roofed on top by its roof type exactly as
// a roof section is (RoofType::build with a RoofInput whose section is the outline, base its height plus
// thickness above the floor's level, no floor above). A type that uses pitch sits at least kRoofThickness above
// the underside, since its own underside reaches that far below the base at the outline and would show below
// the soffit. Its faces go into one part <floor>_<canopy>_Canopy, covering in its covering slot (the canopy's
// material, else the roof or flat_roof default), underside in the soffit slot, edges in the fascia slot, gables
// and parapets in the wall_outside slot, glazing in the glass slot. A flat type with parapet 0 makes a plain
// slab thickness deep over the eaves with a fascia band all round. What blocks a canopy: the footprint of its
// floor and of every floor above whose heights it reaches (an upper floor from the underside of the slab it
// stands on), and the eaves of every roof section of its own floor whose heights it reaches. It stops at the
// outer wall face of the storeys and is cut back to the roof's eaves outline, so the two eaves join without a gap;
// along what blocks it there are no fascia faces and no gable fills, since the wall or the roof closes it there.
// Collision: the slab as convex pieces, and a hull for each convex piece of a roof covering.
// Problems (object the canopy): canopy_outline_invalid (error: the outline crosses itself, or nothing is left of
// it outside the building; it is left out), canopy_type_fallback (warning, as roof_type_fallback and
// roof_type_unknown), canopy_unsupported (warning: no generator item, wall or deck lies under it within 0.3 m of
// its outline; it is built anyway, a cantilever).
//
// Overhanging upper floors (issue 20): a floor whose footprint reaches past the floor below already gets a slab
// with an underside there (Storeys.h, slabBuild), in the Soffit slot (not the ceiling of the room below). The part
// maker "overhangs" (order 41, kind "overhang", group "surfaces"), core/src/parts/makers/OverhangParts.cpp, adds
// for the slab above floor i: the part <floor i>_Overhang, a fascia board 0.02 m deep round the free edges of the
// overhang (overhangArea grown by that and less both footprints), from the slab's underside to its top, in the
// Fascia slot with the roof edges' material (the wall_top default), no collision; roof cuts (BuildParts.h) in
// every roof section of the floors below floor i, from the overhang's underside up, so no lower roof pokes into
// it (the roof of floor i already stops at the footprint above, Roofs.h); and floor_overhang (warning, floor and
// object the upper floor, naming the depth) when the overhang reaches more than kMaxOverhangDepth past the
// footprint of floor i.

inline constexpr double kMaxOverhangDepth = 3.0; // metres

// Checks the values a canopy command would set, with the rules of checkPlan and checkRoofSettings on its roof,
// and refuses an outline that crosses itself. InvalidValue with a plain sentence.
Status checkCanopy(const Canopy& canopy);

}
