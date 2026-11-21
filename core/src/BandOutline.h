#pragma once

#include "plynth/core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace plynth::detail {

// One end of a wall band: where it ends and the unit direction of the wall there, pointing along the wall.
struct BandEnd {
    Vec2 position;
    Vec2 along;
};

// A point plus a vector times a factor.
inline Vec2 plus(Vec2 p, Vec2 v, double k)
{
    return {p.x + v.x * k, p.y + v.y * k};
}

// Dot product.
inline double dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

// The vector made one unit long. A zero vector stays zero.
inline Vec2 unit(Vec2 v)
{
    const double n = std::hypot(v.x, v.y);
    if (n <= 0.0) return {0.0, 0.0};
    return {v.x / n, v.y / n};
}

// The outline of a band around a centre line. start and end say where the band stops and the direction of the
// wall there (the cut is square to that direction). Inner corners use mitres. The outline has a positive
// signedArea. Used for the wall bands and the footprints of openings.
std::vector<Vec2> bandAround(const std::vector<Vec2>& line, double half, const BandEnd& start, const BandEnd& end);

// The shortest distance from a position to a polygon's outline, 0 when the position is inside it.
inline double distanceToPolygon(Vec2 position, const std::vector<Vec2>& polygon)
{
    if (polygon.size() < 3) return 1e300;
    if (insidePolygon(position, polygon)) return 0.0;
    double best = 1e300;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        best = std::min(best, projectOntoSegment(position, polygon[i], polygon[(i + 1) % polygon.size()]).distance);
    }
    return best;
}

// The shortest distance from a position to a polyline.
inline double distanceToLine(Vec2 position, const std::vector<Vec2>& line)
{
    double best = 1e300;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        best = std::min(best, projectOntoSegment(position, line[i], line[i + 1]).distance);
    }
    return best;
}

}
