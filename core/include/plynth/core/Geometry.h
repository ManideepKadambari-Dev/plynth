#pragma once

#include "Plan.h"
#include "Status.h"

#include <span>
#include <vector>

namespace plynth {

// A position on the plan in metres (x right, y down, see Plan.h), or in image pixels where a name says so.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const Vec2&) const = default;
};

// The two image pixel points must be at least this far apart to measure a scale from them.
inline constexpr double kMinScalePixelDistance = 1.0;

// Straight line distance between two positions.
double distance(Vec2 a, Vec2 b);

// Where a point sits.
Vec2 positionOf(const Point& point);

// Length of the straight line between a wall's two points. 0 when a point is missing.
// Bulge is ignored until curved walls arrive in milestone 5.
double wallLength(const Floor& floor, const Wall& wall);

// The closest place on the segment from a to b to a position.
struct SegmentProjection {
    Vec2 position;
    double offset = 0.0;   // distance from a along the segment, from 0 to its length
    double distance = 0.0; // distance from the position to the segment
};

// Finds the closest place on the segment from a to b. For a zero length segment that is a.
SegmentProjection projectOntoSegment(Vec2 position, Vec2 a, Vec2 b);

// Area of a closed polygon by the shoelace formula. Positive when the corners run clockwise on screen
// (with y pointing down), negative when they run the other way.
double signedArea(std::span<const Vec2> polygon);

// Says whether a position is inside a closed polygon, by the even odd rule. A position exactly on an
// edge may count either way.
bool insidePolygon(Vec2 position, std::span<const Vec2> polygon);

// The scale measured from two points on the plan image and the real distance between them.
struct ScaleMeasure {
    Status status;
    double pixelsPerMetre = 0.0;
    double pixelDistance = 0.0;
};

// Works out pixels per metre from two image pixel points and the real distance between them in metres.
// InvalidValue when a number is not finite, the distance is not above zero, or the points are closer than
// kMinScalePixelDistance.
ScaleMeasure scaleFromImagePoints(Vec2 firstPixel, Vec2 secondPixel, double metres);

}
