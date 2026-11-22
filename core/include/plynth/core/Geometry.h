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

// Length of a wall along its true centre line: the straight distance, or the arc length for a curved wall
// (see wallCurve in WallGeometry.h). Rounded corners do not change it. 0 when a point is missing.
double wallLength(const Floor& floor, const Wall& wall);

// The unit direction a quarter turn to the right of a direction on the plan: (-d.y, d.x). See WallSide.
Vec2 rightOf(Vec2 direction);

// The rule for cutting a true circular arc into straight segments. Walls, rounded corners and opening shapes
// all use it, in the app, the CLI and later the Blender build, so every part shows the same corners.
// Points are spread evenly by angle, the first and last points are exactly the arc's ends.
inline constexpr double kArcMaxSegmentAngle = 0.130899693899574718; // 7.5 degrees in radians
inline constexpr double kArcMaxDeviation = 0.005;                    // metres between a segment and the arc
inline constexpr int kArcMinSegments = 2;
inline constexpr int kArcMaxSegments = 256;

// How many segments an arc of this radius (metres) and sweep (radians, either sign) is cut into: the
// smallest count where no segment spans more than kArcMaxSegmentAngle and no segment strays more than
// kArcMaxDeviation from the arc, kept between kArcMinSegments and kArcMaxSegments. The deviation of a segment
// spanning angle a is radius * (1 - cos(a / 2)). kArcMinSegments for a sweep of 0 or a radius that is not
// finite and above zero.
int arcSegmentCount(double radius, double sweep);

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

// Turns a pixel of a floor's plan image into plan metres and back, with the floor's image offset
// (ImageOffset in Plan.h). pixelsPerMetre must be above zero. Every place that turns image pixels into metres
// uses these, so each floor's image can sit somewhere else.
Vec2 imageToPlan(const Floor& floor, double pixelsPerMetre, Vec2 pixel);
Vec2 planToImage(const Floor& floor, double pixelsPerMetre, Vec2 position);

// Works out pixels per metre from two image pixel points and the real distance between them in metres.
// InvalidValue when a number is not finite, the distance is not above zero, or the points are closer than
// kMinScalePixelDistance.
ScaleMeasure scaleFromImagePoints(Vec2 firstPixel, Vec2 secondPixel, double metres);

}
