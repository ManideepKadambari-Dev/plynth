#pragma once

#include "Geometry.h"
#include "Plan.h"

#include <optional>
#include <vector>

namespace plynth {

// What a snapped position landed on.
enum class SnapKind { None, Grid, Point, Wall, Align };

// How to snap a position while drawing or dragging. All lengths in metres.
struct SnapOptions {
    double gridSize = 0.0;              // 0 or less turns grid snapping off
    double radius = 0.0;                // how close a point or wall must be to snap to it; 0 turns that off
    bool toPoints = true;
    bool toWalls = true;
    std::optional<Vec2> orthogonalFrom; // when set, keeps the result on the horizontal or vertical line through it
    std::vector<Id> ignorePoints;       // never snapped to, nor the walls that touch them (the point being dragged)
    bool toAlignment = true;            // line up with the x or y of a point within radius
    std::vector<Vec2> alignTo;          // more places to line up with, such as the start of a wall not made yet
};

// A snapped position and what it snapped to.
struct SnapResult {
    Vec2 position;
    SnapKind kind = SnapKind::None;
    Id pointId;              // for Point
    Id wallId;               // for Wall
    double wallOffset = 0.0; // for Wall: distance along the wall from its from point
    std::optional<Vec2> alignedX; // for Align and Wall: the point or place whose x the position took, for a guide line
    std::optional<Vec2> alignedY; // for Align and Wall: the point or place whose y the position took
};

// Snaps a position on a floor. In order:
// 1. With orthogonalFrom, the position moves onto the horizontal or the vertical line through it,
//    whichever is closer.
// 2. The nearest point within radius wins, even over the orthogonal line, so a loop can always be closed.
// 3. Otherwise the nearest wall within radius of its true centre line (the arc for a curved wall, see
//    projectOntoCurve), at the closest place strictly between its two points. wallOffset is measured along
//    that line. Without orthogonalFrom and with toAlignment, when the x or y of a point (or alignTo place) is
//    within radius of the position and the vertical or horizontal line through it crosses the wall within radius
//    of the position, the place moves to that crossing instead, the one closest to the position, with alignedX or
//    alignedY set. So a wall ending on another wall comes out straight.
// 4. Otherwise, with toAlignment, the x of the position becomes the x of the point (not ignored) or alignTo place
//    whose x is nearest within radius, and the same for y, so walls come out straight and corners square. With
//    orthogonalFrom only the free coordinate lines up. A coordinate that lines up with nothing is rounded to the
//    grid when that is on. Kind Align, with alignedX and alignedY saying what each coordinate lined up with.
// 5. Otherwise the grid: each coordinate rounded to a multiple of gridSize, the grid starting at the image
//    corner (0, 0). With orthogonalFrom only the free coordinate is rounded.
// 6. Otherwise the position from step 1 with kind None.
SnapResult snapPosition(const Floor& floor, Vec2 position, const SnapOptions& options);

// What is under a position.
enum class PickKind { None, Point, Wall, Room, Opening, Item };

struct Pick {
    PickKind kind = PickKind::None;
    Id id;

    bool operator==(const Pick&) const = default;
};

// Finds what a click at a position means: the nearest point within radius, else an opening whose
// footprint (OpeningPlacement::footprint in Openings.h) is within radius of the position, else an item whose
// footprint (itemFootprint in Items.h) holds the position or is within radius of it (the one drawn on top,
// that is the last in the floor's list, when several do), else the nearest wall whose band (wallBand in WallGeometry.h) is within radius of the position, else the room whose area
// holds it (see roomAt in Rooms.h), else nothing. radius is in metres; the app turns its pixel radius into
// metres.
Pick pickAt(const Floor& floor, Vec2 position, double radius);

}
