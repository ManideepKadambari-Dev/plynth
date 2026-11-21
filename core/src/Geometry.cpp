#include "plynth/core/Geometry.h"

#include <algorithm>
#include <cmath>

namespace plynth {

// Straight line distance between two positions.
double distance(Vec2 a, Vec2 b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

// Where a point sits.
Vec2 positionOf(const Point& point)
{
    return {point.x, point.y};
}

// Length of the straight line between the wall's two points, 0 when one is missing.
double wallLength(const Floor& floor, const Wall& wall)
{
    const Point* a = findPoint(floor, wall.from);
    const Point* b = findPoint(floor, wall.to);
    if (!a || !b) return 0.0;
    return distance(positionOf(*a), positionOf(*b));
}

// The unit direction a quarter turn to the right.
Vec2 rightOf(Vec2 direction)
{
    return {-direction.y, direction.x};
}

// Works out how many segments an arc is cut into.
int arcSegmentCount(double radius, double sweep)
{
    const double angle = std::fabs(sweep);
    if (!std::isfinite(radius) || radius <= 0.0 || !std::isfinite(angle) || angle <= 0.0) return kArcMinSegments;

    double needed = std::ceil(angle / kArcMaxSegmentAngle - 1e-9);
    // Deviation of one segment spanning a is radius * (1 - cos(a / 2)), so it stays within the limit
    // while a / 2 is at most acos(1 - limit / radius).
    const double ratio = kArcMaxDeviation / radius;
    if (ratio < 2.0) {
        const double half = std::acos(1.0 - ratio);
        if (half <= 0.0) return kArcMaxSegments;
        needed = std::max(needed, std::ceil(angle / (2.0 * half) - 1e-9));
    }
    if (!(needed < kArcMaxSegments)) return kArcMaxSegments;
    int count = std::max(kArcMinSegments, static_cast<int>(needed));
    const auto deviation = [&](int n) { return radius * (1.0 - std::cos(angle / n / 2.0)); };
    while (count < kArcMaxSegments && deviation(count) > kArcMaxDeviation * (1.0 + 1e-12)) ++count;
    return count;
}

// Finds the closest place on the segment to a position.
SegmentProjection projectOntoSegment(Vec2 position, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    SegmentProjection result;
    if (lengthSquared <= 0.0) {
        result.position = a;
        result.offset = 0.0;
        result.distance = distance(position, a);
        return result;
    }
    const double length = std::sqrt(lengthSquared);
    const double t = std::clamp(((position.x - a.x) * dx + (position.y - a.y) * dy) / lengthSquared, 0.0, 1.0);
    result.position = {a.x + dx * t, a.y + dy * t};
    result.offset = t * length;
    result.distance = distance(position, result.position);
    return result;
}

// Shoelace formula, positive for corners running clockwise on screen.
double signedArea(std::span<const Vec2> polygon)
{
    double sum = 0.0;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = polygon[i];
        const Vec2 b = polygon[(i + 1) % n];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum / 2.0;
}

// Counts how many edges a ray to the right crosses; an odd count means inside.
bool insidePolygon(Vec2 position, std::span<const Vec2> polygon)
{
    bool inside = false;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0, j = n ? n - 1 : 0; i < n; j = i++) {
        const Vec2 a = polygon[i];
        const Vec2 b = polygon[j];
        if ((a.y > position.y) != (b.y > position.y)) {
            const double crossX = (b.x - a.x) * (position.y - a.y) / (b.y - a.y) + a.x;
            if (position.x < crossX) inside = !inside;
        }
    }
    return inside;
}

// Works out pixels per metre from two image points and the real distance between them.
ScaleMeasure scaleFromImagePoints(Vec2 firstPixel, Vec2 secondPixel, double metres)
{
    ScaleMeasure result;
    const bool finite = std::isfinite(firstPixel.x) && std::isfinite(firstPixel.y) && std::isfinite(secondPixel.x)
        && std::isfinite(secondPixel.y) && std::isfinite(metres);
    if (!finite) {
        result.status = fail(ErrorCode::InvalidValue, "The points and the distance must be finite numbers.");
        return result;
    }
    if (metres <= 0.0) {
        result.status = fail(ErrorCode::InvalidValue, "The real distance must be above zero.");
        return result;
    }
    const double pixels = distance(firstPixel, secondPixel);
    if (pixels < kMinScalePixelDistance) {
        result.status = fail(ErrorCode::InvalidValue, "The two points are too close together to measure a scale.");
        return result;
    }
    result.pixelDistance = pixels;
    result.pixelsPerMetre = pixels / metres;
    return result;
}

}
