#pragma once

#include "Geometry.h"
#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <vector>

namespace plynth {

// Openings sit on one wall each. The plan view sets where along the wall (offset), the wall view sets how
// high (sill). Lengths in metres. Wall face coordinates: u along the wall's true centre line from its from
// point, v up from the floor.

// The step new and dragged openings snap to, along the wall and up the wall, unless the user places freely.
inline constexpr double kOpeningSnapStep = 0.05;

// Two openings closer than this along or up the wall still count as touching, not overlapping.
inline constexpr double kOpeningTouchTolerance = 1e-6;

// A wall continuing straight on at a point (within this angle) leaves no corner there for clearance.
inline constexpr double kInLineAngle = 0.174532925199432958; // 10 degrees in radians

// Where on a wall openings may go.
struct WallSpan {
    double start = 0.0;  // u where the free part of the wall starts
    double end = 0.0;    // u where it ends; end - start may be zero or less on a short wall
    double height = 0.0; // the wall's height (wallHeight)
};

// The free part of a wall. At each end the clearance is the larger of:
// - the trim of a rounded corner there (WallPath::startTrim or endTrim), and
// - half the thickness of the thickest other wall at that point that does not continue in line (its
//   direction leaving the point is more than kInLineAngle away from straight on). A free end has none.
// start is the clearance at the from end, end is the wall's length minus the clearance at the to end.
WallSpan wallSpan(const Floor& floor, const Wall& wall);

// The sizes a new opening of a type gets from the project's defaults: a door gets door width and height
// with sill 0, a window gets window width, height and sill, a plain opening gets door width and height with
// sill 0. Shape rectangle, hinge Start, swing Right, no frame, no id, no wall, offset 0.
Opening defaultOpening(const DefaultSizes& sizes, OpeningType type);

// Checks whether an opening can be where it says. The other openings are the floor's openings with a
// different id, so an edited copy of an opening already on the floor is checked against the rest only.
// In order:
// - NotFound when its wall is not on the floor.
// - InvalidValue when offset, width, height or sill is not finite, width or height is not above zero, or
//   sill is below zero.
// - InvalidValue from checkOpeningShape (unknown shape, or a size the shape cannot take).
// - InvalidValue when it reaches past the wall's free part: offset - width / 2 below wallSpan start, or
//   offset + width / 2 above wallSpan end (more than kOpeningTouchTolerance). The message says by how much.
// - InvalidValue when sill + height is above the wall's height.
// - InvalidValue when its box (u from offset - width / 2 to offset + width / 2, v from sill to sill + height)
//   overlaps the box of another opening on the same wall by more than kOpeningTouchTolerance both ways. The
//   message names the other opening. Boxes are used, not outlines, so a window may sit above a door.
Status checkOpeningFit(const Floor& floor, const Opening& opening);

// An opening that no longer fits, for example after its wall got shorter, bent or lowered.
struct OpeningProblem {
    Id opening;
    Status status; // what checkOpeningFit says
};

// Every opening of the floor that checkOpeningFit refuses, in the floor's order. The app draws them in Signal
// amber and the CLI lists them; the Blender build reports them in milestone 8.
std::vector<OpeningProblem> openingProblems(const Floor& floor);

// The same opening moved and sized so it sits inside its wall: offset kept within the free part (the middle
// of the free part when the opening is wider than it), sill kept from 0 up to the wall's height minus the
// opening's height (0 when the opening is taller than the wall). Width, height and overlaps are not changed.
// The opening as it is when its wall is not on the floor. The wall view uses it while dragging.
Opening fitOpeningBox(const Floor& floor, Opening opening);

// Where an opening is on the plan.
struct OpeningPlacement {
    bool valid = false;  // false when its wall is not on the floor or the wall's curve is not valid
    Vec2 centre;         // on the true centre line at offset
    Vec2 start;          // the Start jamb on the centre line, at offset - width / 2
    Vec2 end;            // the End jamb on the centre line, at offset + width / 2
    Vec2 startTangent;   // unit directions along the wall at the jambs, toward the wall's to point
    Vec2 endTangent;
    double thickness = 0.0;      // the wall's thickness
    std::vector<Vec2> centreLine; // from start to end along the wall (curvePoints)
    // The cut through the wall band: centreLine offset by half the thickness on each side, closed, positive
    // signedArea. Drawn as the gap in the wall and used by pickAt.
    std::vector<Vec2> footprint;
};

// Works out where an opening is on the plan. The jambs are clamped to the wall's ends.
OpeningPlacement openingPlacement(const Floor& floor, const Opening& opening);

// A wall and a place along it for a new opening, from a position on the plan.
struct OpeningSpot {
    Status status;
    Id wall;
    double offset = 0.0;
};

// Finds where an opening of this width would go for a position: the wall whose band is nearest the position
// and within radius of it, at the place along its true centre line closest to the position, rounded to a
// multiple of step (step 0 or less does not round) and then kept so the whole width is inside the wall's free
// part. NotFound when no wall is within radius ("There is no wall here."). InvalidValue when width is not
// finite and above zero, or the free part is narrower than width (the message gives the free length).
// The app's Opening tool and the CLI "opening add --x --y" both use it, so a click and a command agree.
OpeningSpot placeOpeningAt(const Floor& floor, Vec2 position, double radius, double width, double step);

}
