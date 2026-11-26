#pragma once

#include "Geometry.h"
#include "Plan.h"

#include <optional>
#include <utility>
#include <vector>

namespace plynth {

// Rooms come from the wall graph of a floor: points are its nodes, walls (straight or curved) its edges.
// Rules for finding closed areas:
// - Walls that end loose (dangling) and walls with the same area on both sides (a bridge between two rings)
//   are not part of any area's rings.
// - A ring that sits inside an area without touching it (an island, such as a room inside a room) becomes a
//   hole of the smallest area around it. The island's own inside is a separate area.
// - Walls that cross without sharing a point do not split an area. Zero length walls are skipped.
// - At each point the walls are ordered by the direction they leave it: the tangent of a curved wall at that
//   end (tangentAlong in WallGeometry.h), so a curved wall sorts by where it really heads, not by its chord.
// - The corners of a ring are the walls' drawn centre lines (WallPath::centreLine) joined in ring order, so
//   arcs and rounded corners are part of the ring and of every area worked out from it.

// One closed area of the wall graph.
struct ClosedArea {
    std::vector<Id> walls;                // the outer ring in order, then the ring of each island in order
    std::vector<std::vector<Vec2>> rings; // the centre line corners of each ring, same order; rings[0] is outer
    double centreLineArea = 0.0;          // the outer ring minus the islands, on the centre lines, square metres
    Vec2 inside;                          // a position well inside the area, usable for "room add"
};

// Finds every closed area made by the walls of a floor, in a stable order (by the smallest wall id of
// the outer ring). Two calls on equal floors give equal results.
std::vector<ClosedArea> findClosedAreas(const Floor& floor);

// The closed area that holds a position, islands excluded. Empty when none does, for example outside the
// building or on a centre line.
std::optional<ClosedArea> findClosedAreaAt(const Floor& floor, Vec2 position);

// The shape of a room as its walls stand now.
struct RoomShape {
    // True when the room's walls form closed rings. False when a wall is missing or the rings do not
    // close, for example in a file written by another tool.
    bool closed = false;
    std::vector<std::vector<Vec2>> rings; // centre line rings, outer first, as in ClosedArea
    // The floor inside the walls: the area of the rings minus the band of each of the room's walls (wallBand
    // in WallGeometry.h: half the thickness on each side of the drawn centre line, half the thickness past
    // each plain end). Rings with a positive signedArea are outlines, negative ones are holes.
    std::vector<std::vector<Vec2>> floorArea;
    double area = 0.0; // square metres of floorArea
    Vec2 labelPoint;   // a position well inside floorArea, for the room's name and area
};

// Works out the shape of a room from its walls. Not closed, with area 0, when the rings do not close.
RoomShape roomShape(const Floor& floor, const Room& room);

// The room whose centre line rings hold a position, islands excluded. Empty when there is none.
Id roomAt(const Floor& floor, Vec2 position);

// Says whether a position is inside an area: inside its outer ring and outside every island ring.
bool insideArea(const ClosedArea& area, Vec2 position);

// The two closed areas a room becomes when a wall not in it cuts through its inside and meets its ring at both
// ends, on its own or together with loose walls already inside the room. Each area holds at least one wall of
// the room. Empty when the wall does not cut the room in two: it touches the room at one point only, runs
// outside it or along a hole, or the room's walls do not close.
std::optional<std::pair<ClosedArea, ClosedArea>> roomSplitBy(const Floor& floor, const Room& room, const Id& wallId);

}
