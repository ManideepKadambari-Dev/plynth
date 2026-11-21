#pragma once

#include "Geometry.h"
#include "Plan.h"

#include <optional>
#include <vector>

namespace plynth {

// What a snapped position landed on.
enum class SnapKind { None, Grid, Point, Wall };

// How to snap a position while drawing or dragging. All lengths in metres.
struct SnapOptions {
    double gridSize = 0.0;              // 0 or less turns grid snapping off
    double radius = 0.0;                // how close a point or wall must be to snap to it; 0 turns that off
    bool toPoints = true;
    bool toWalls = true;
    std::optional<Vec2> orthogonalFrom; // when set, keeps the result on the horizontal or vertical line through it
    std::vector<Id> ignorePoints;       // never snapped to, nor the walls that touch them (the point being dragged)
};

// A snapped position and what it snapped to.
struct SnapResult {
    Vec2 position;
    SnapKind kind = SnapKind::None;
    Id pointId;              // for Point
    Id wallId;               // for Wall
    double wallOffset = 0.0; // for Wall: distance along the wall from its from point
};

// Snaps a position on a floor. In order:
// 1. With orthogonalFrom, the position moves onto the horizontal or the vertical line through it,
//    whichever is closer.
// 2. The nearest point within radius wins, even over the orthogonal line, so a loop can always be closed.
// 3. Otherwise the nearest wall within radius of its centre line, at the closest place strictly between its
//    two points.
// 4. Otherwise the grid: each coordinate rounded to a multiple of gridSize, the grid starting at the image
//    corner (0, 0). With orthogonalFrom only the free coordinate is rounded.
// 5. Otherwise the position from step 1 with kind None.
SnapResult snapPosition(const Floor& floor, Vec2 position, const SnapOptions& options);

// What is under a position.
enum class PickKind { None, Point, Wall, Room };

struct Pick {
    PickKind kind = PickKind::None;
    Id id;

    bool operator==(const Pick&) const = default;
};

// Finds what a click at a position means: the nearest point within radius, else the nearest wall whose
// band (half its thickness plus radius) holds the position, else the room whose area holds it (see roomAt
// in Rooms.h), else nothing. radius is in metres; the app turns its pixel radius into metres.
Pick pickAt(const Floor& floor, Vec2 position, double radius);

}
