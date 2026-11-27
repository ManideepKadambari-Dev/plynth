#pragma once

#include "../PlanRingClean.h"
#include "plynth/core/BuildGeometry.h"
#include "plynth/core/Plan.h"
#include "plynth/core/Storeys.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Small shape helpers the part makers share. Clipper work is done in world X and Y, where every ring keeps the
// turning it had on the plan.
namespace plynth::partgeometry {

// Below this many square metres a piece is nothing.
inline constexpr double kTinyPartArea = 1e-6;

// The points of an outline drawn on the plan.
inline std::vector<Vec2> pointsOf(const std::vector<PlanXY>& outline)
{
    std::vector<Vec2> out;
    out.reserve(outline.size());
    for (const PlanXY& p : outline) out.push_back({p.x, p.y});
    return out;
}

// A plan ring in world X and Y: y turned into -y and the points reversed, so it keeps its turning.
inline std::vector<Vec2> worldRingOf(const std::vector<Vec2>& ring)
{
    std::vector<Vec2> out;
    out.reserve(ring.size());
    for (auto it = ring.rbegin(); it != ring.rend(); ++it) out.push_back({it->x, -it->y});
    return out;
}

// A plan area as a world polygon.
inline WorldPolygon worldPolygonOf(const PlanArea& area)
{
    WorldPolygon out;
    out.outline = worldRingOf(area.outline);
    for (const std::vector<Vec2>& hole : area.holes) out.holes.push_back(worldRingOf(hole));
    return out;
}

// Plan areas as world polygons.
inline std::vector<WorldPolygon> worldPolygonsOf(const std::vector<PlanArea>& areas)
{
    std::vector<WorldPolygon> out;
    for (const PlanArea& area : areas) out.push_back(worldPolygonOf(area));
    return out;
}

// A ring as a Clipper path.
inline Clipper2Lib::PathD pathOf(const std::vector<Vec2>& ring)
{
    Clipper2Lib::PathD path;
    path.reserve(ring.size());
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

// The outlines and holes of polygons as Clipper paths.
inline Clipper2Lib::PathsD pathsOf(const std::vector<WorldPolygon>& polygons)
{
    Clipper2Lib::PathsD paths;
    for (const WorldPolygon& poly : polygons) {
        paths.push_back(pathOf(poly.outline));
        for (const std::vector<Vec2>& hole : poly.holes) paths.push_back(pathOf(hole));
    }
    return paths;
}

// The area of a polygon, its holes taken off.
inline double areaOf(const WorldPolygon& polygon)
{
    double size = ringAreaOf(polygon.outline);
    for (const std::vector<Vec2>& hole : polygon.holes) size += ringAreaOf(hole);
    return size;
}

// The area of polygons together.
inline double areaOf(const std::vector<WorldPolygon>& polygons)
{
    double size = 0.0;
    for (const WorldPolygon& polygon : polygons) size += areaOf(polygon);
    return size;
}

// The area of plain Clipper paths, holes counted negative.
inline double areaOf(const Clipper2Lib::PathsD& paths)
{
    double size = 0.0;
    for (const Clipper2Lib::PathD& path : paths) size += ringAreaOf(ringOfPath(path));
    return size;
}

// Plain Clipper paths read back as polygons by their turning, cleaned of rounding noise, without slivers.
inline std::vector<WorldPolygon> polygonsOf(const Clipper2Lib::PathsD& paths)
{
    std::vector<WorldPolygon> out;
    for (const RingPiece& piece : piecesByTurning(paths)) {
        WorldPolygon polygon;
        polygon.outline = cleanedRingOf(piece.outline);
        if (polygon.outline.size() < 3) continue;
        for (const std::vector<Vec2>& points : piece.holes) {
            std::vector<Vec2> hole = cleanedRingOf(points);
            if (hole.size() >= 3) polygon.holes.push_back(std::move(hole));
        }
        if (areaOf(polygon) < kTinyPartArea) continue;
        out.push_back(std::move(polygon));
    }
    return out;
}

// The ring turned so it runs anticlockwise.
inline std::vector<Vec2> anticlockwise(std::vector<Vec2> ring)
{
    if (ringAreaOf(ring) < 0.0) std::reverse(ring.begin(), ring.end());
    return ring;
}

// The ring without points that repeat the one before them.
inline std::vector<Vec2> withoutRepeatedPoints(const std::vector<Vec2>& ring)
{
    std::vector<Vec2> out;
    for (const Vec2& p : ring) {
        if (!out.empty() && std::hypot(p.x - out.back().x, p.y - out.back().y) < 1e-9) continue;
        out.push_back(p);
    }
    while (out.size() > 1 && std::hypot(out.back().x - out.front().x, out.back().y - out.front().y) < 1e-9) out.pop_back();
    return out;
}

// Which side of the line from o through a the point b lies on: positive to the left.
inline double turnOf(Vec2 o, Vec2 a, Vec2 b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Says whether p, known to be in line with a and b, lies between them.
inline bool withinSegment(Vec2 p, Vec2 a, Vec2 b)
{
    constexpr double kSlack = 1e-9;
    return p.x >= std::min(a.x, b.x) - kSlack && p.x <= std::max(a.x, b.x) + kSlack &&
           p.y >= std::min(a.y, b.y) - kSlack && p.y <= std::max(a.y, b.y) + kSlack;
}

// Says whether the segments from a to b and from c to d cross or touch.
inline bool segmentsMeet(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    constexpr double kFlat = 1e-12;
    const double d1 = turnOf(c, d, a);
    const double d2 = turnOf(c, d, b);
    const double d3 = turnOf(a, b, c);
    const double d4 = turnOf(a, b, d);
    const bool apartAB = (d1 > kFlat && d2 < -kFlat) || (d1 < -kFlat && d2 > kFlat);
    const bool apartCD = (d3 > kFlat && d4 < -kFlat) || (d3 < -kFlat && d4 > kFlat);
    if (apartAB && apartCD) return true;
    if (std::fabs(d1) <= kFlat && withinSegment(a, c, d)) return true;
    if (std::fabs(d2) <= kFlat && withinSegment(b, c, d)) return true;
    if (std::fabs(d3) <= kFlat && withinSegment(c, a, b)) return true;
    if (std::fabs(d4) <= kFlat && withinSegment(d, a, b)) return true;
    return false;
}

// Says whether a closed ring crosses or touches itself anywhere but where neighbouring edges meet. Points that
// repeat the one before them are not counted.
inline bool crossesItself(const std::vector<Vec2>& points)
{
    const std::vector<Vec2> ring = withoutRepeatedPoints(points);
    const std::size_t n = ring.size();
    if (n < 4) return false;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            if (segmentsMeet(ring[i], ring[(i + 1) % n], ring[j], ring[(j + 1) % n])) return true;
        }
    }
    return false;
}

// The distance from a point to the nearest edge of any ring of the polygons.
inline double distanceToEdges(Vec2 p, const std::vector<WorldPolygon>& polygons)
{
    double nearest = 1e300;
    const auto consider = [&](const std::vector<Vec2>& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            nearest = std::min(nearest, distanceToSegmentOf(p, ring[i], ring[(i + 1) % ring.size()]));
        }
    };
    for (const WorldPolygon& polygon : polygons) {
        consider(polygon.outline);
        for (const std::vector<Vec2>& hole : polygon.holes) consider(hole);
    }
    return nearest;
}

}
