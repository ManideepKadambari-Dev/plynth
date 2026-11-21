#include "plynth/core/Snapping.h"

#include "plynth/core/Rooms.h"
#include "plynth/core/commands/WallCommands.h"

#include <algorithm>
#include <cmath>

namespace plynth {

namespace {

// Says whether an id is in the list.
bool listed(const std::vector<Id>& ids, const Id& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Rounds a value to the nearest multiple of step, without a negative zero.
double roundToGrid(double value, double step)
{
    return std::round(value / step) * step + 0.0;
}

}

// Snaps by the rule list in Snapping.h.
SnapResult snapPosition(const Floor& floor, Vec2 position, const SnapOptions& options)
{
    SnapResult result;
    Vec2 adjusted = position;
    bool keepsY = false;
    if (options.orthogonalFrom) {
        const Vec2 from = *options.orthogonalFrom;
        keepsY = std::abs(position.y - from.y) <= std::abs(position.x - from.x);
        if (keepsY) {
            adjusted.y = from.y;
        } else {
            adjusted.x = from.x;
        }
    }
    result.position = adjusted;

    const double radius = std::max(options.radius, 0.0);
    if (radius > 0.0 && options.toPoints) {
        const Point* best = nullptr;
        double bestDistance = radius;
        for (const Point& p : floor.points) {
            if (listed(options.ignorePoints, p.id)) continue;
            const double d = distance(position, positionOf(p));
            if (d <= bestDistance) {
                bestDistance = d;
                best = &p;
            }
        }
        if (best) {
            result.position = positionOf(*best);
            result.kind = SnapKind::Point;
            result.pointId = best->id;
            return result;
        }
    }

    // Curved walls cannot be split yet, so they are not snapped to.
    if (radius > 0.0 && options.toWalls) {
        const Wall* best = nullptr;
        SegmentProjection bestProjection;
        double bestDistance = radius;
        for (const Wall& w : floor.walls) {
            if (w.bulge != 0.0) continue;
            if (listed(options.ignorePoints, w.from) || listed(options.ignorePoints, w.to)) continue;
            const Point* a = findPoint(floor, w.from);
            const Point* b = findPoint(floor, w.to);
            if (!a || !b) continue;
            const double length = distance(positionOf(*a), positionOf(*b));
            const SegmentProjection projection = projectOntoSegment(position, positionOf(*a), positionOf(*b));
            if (projection.offset < kMinSplitDistance || projection.offset > length - kMinSplitDistance) continue;
            if (projection.distance <= bestDistance) {
                bestDistance = projection.distance;
                bestProjection = projection;
                best = &w;
            }
        }
        if (best) {
            result.position = bestProjection.position;
            result.kind = SnapKind::Wall;
            result.wallId = best->id;
            result.wallOffset = bestProjection.offset;
            return result;
        }
    }

    if (options.gridSize > 0.0 && std::isfinite(options.gridSize)) {
        if (!options.orthogonalFrom) {
            result.position = {roundToGrid(adjusted.x, options.gridSize), roundToGrid(adjusted.y, options.gridSize)};
        } else if (keepsY) {
            result.position.x = roundToGrid(adjusted.x, options.gridSize);
        } else {
            result.position.y = roundToGrid(adjusted.y, options.gridSize);
        }
        result.kind = SnapKind::Grid;
    }
    return result;
}

// Finds what is under a position: a point, then a wall band, then a room.
Pick pickAt(const Floor& floor, Vec2 position, double radius)
{
    radius = std::max(radius, 0.0);
    const Point* bestPoint = nullptr;
    double bestDistance = radius;
    for (const Point& p : floor.points) {
        const double d = distance(position, positionOf(p));
        if (d <= bestDistance) {
            bestDistance = d;
            bestPoint = &p;
        }
    }
    if (bestPoint) return {PickKind::Point, bestPoint->id};

    const Wall* bestWall = nullptr;
    double bestWallDistance = 0.0;
    for (const Wall& w : floor.walls) {
        const Point* a = findPoint(floor, w.from);
        const Point* b = findPoint(floor, w.to);
        if (!a || !b) continue;
        const double d = projectOntoSegment(position, positionOf(*a), positionOf(*b)).distance;
        if (d > w.thickness / 2.0 + radius) continue;
        if (!bestWall || d < bestWallDistance) {
            bestWallDistance = d;
            bestWall = &w;
        }
    }
    if (bestWall) return {PickKind::Wall, bestWall->id};
    if (Id room = roomAt(floor, position); !room.empty()) return {PickKind::Room, std::move(room)};
    return {};
}

}
