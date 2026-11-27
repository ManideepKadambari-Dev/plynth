#pragma once

#include "BuildGeometry.h"
#include "BuildParts.h"
#include "Plan.h"
#include "Roofs.h"
#include "Status.h"

#include <optional>
#include <string>
#include <vector>

namespace plynth {

// Dormers (issue 15, plan format 8): the part maker "dormers" (order 50, kind "dormer", group "surfaces"),
// core/src/parts/makers/DormerParts.cpp.
//
// A dormer stands on the covering face of a pitched roof section of its floor that holds (x, y) seen from above.
// Its front is vertical, faces down the slope of that face (the face's steepest descent turned to the plan), and
// stands where the slope passes through (x, y). It reaches at least depth back into the roof, where its own roof
// meets the main roof plane, so its front is as high as that gives (depth times the main roof's slope less half the
// width times the dormer's slope for a gable, depth times the difference of the two slopes for a shed), but never
// lower than its window needs: then it reaches further back until its own roof meets the main roof. Its cheeks (side
// walls) run from the front back to where they meet the main roof. Its own roof is a gable (ridge running back
// into the main roof) or a shed (falling toward the front) at its pitch, kRoofThickness thick, with
// kDormerOverhang past the front and cheeks, and reaches back until its top meets the main roof. The front and
// the cheeks stand down to the main roof's underside, so they close the edge of the cut. The window: a framed
// window windowWidth by windowHeight in the middle of the front, windowSill above the front's foot, built as part
// prisms (frame in the frame slot, pane in the glass slot), so no opening fill code is needed. The front must
// reach 0.1 m above the window and keep 0.05 m on each side of it.
// The main roof gets a RoofCut: the dormer's footprint on the roof plane, so the covering and its underside are
// open where the dormer stands.
// Parts: one part <floor>_<dormer>_Dormer with the cheeks and front (wall_outside slot or its material), its roof
// (roof slot or roof material), the window; collision as convex pieces of the box and its roof.
// Problems, each an error and the dormer not built: dormer_type_unknown (the type is not gable or shed),
// dormer_not_on_roof ((x, y) is on no pitched covering face of the floor's roof sections), dormer_window_too_big
// (the window is too wide for the front, or a shed is as steep as the roof under it), dormer_too_big (it reaches past the covering face it stands on, so past the ridge, a hip, a
// valley or the eaves, or past the section's outline; its own roof behind the front counts too; the sentence says
// when the window made it reach further back), dormer_overlap
// (two dormers overlap, own roofs included; the later one is not built). They are checked in that order.

inline constexpr double kDormerOverhang = 0.15; // metres
inline constexpr double kDormerWallThickness = 0.15;

// Where a dormer would stand, for the plan's outline while placing it and for the checks.
struct DormerPlacement {
    Status status;      // the errors above but overlap, as InvalidValue with the same sentence
    int roofIndex = 0;  // RoofBuild::index of the section
    Vec2 front;         // world X and Y of the middle of the front's foot
    Vec2 facing;        // unit world direction the front faces
    double footZ = 0.0; // world Z of the front's foot on the roof
    // The cut on the plan, world X and Y, anticlockwise. Empty for an unknown type or off every pitched roof.
    std::vector<Vec2> footprint;
};

// Works out where a dormer of a floor stands on that floor's roofs (floorRoofs at levels).
DormerPlacement dormerPlacement(const Plan& plan, const Floor& floor, const Dormer& dormer,
                                const std::vector<double>& levels);

// Checks the values a dormer command would set, with the rules and sentences of checkPlan. The id is not checked.
Status checkDormer(const Dormer& dormer);

}
