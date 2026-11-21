#include "plynth/core/Snapping.h"

#include "BandOutline.h"

#include "plynth/core/Items.h"
#include "plynth/core/Openings.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"
#include "plynth/core/commands/WallCommands.h"

#include <algorithm>
#include <cmath>

namespace plynth {

using detail::distanceToLine;
using detail::distanceToPolygon;

namespace {

// Says whether an id is in the list.
bool listed(const std::vector<Id>& ids, const Id& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Finds where the horizontal line y = line (keepsY) or the vertical line x = line crosses a wall's true centre
// line, strictly between its two ends, and takes the crossing nearest to a position. Returns false when there is
// none.
bool crossingNear(const WallCurve& curve, bool keepsY, double line, Vec2 position, Vec2& place, double& offset)
{
    std::vector<std::pair<Vec2, double>> found;
    if (!curve.curved) {
        const double along = keepsY ? curve.to.y - curve.from.y : curve.to.x - curve.from.x;
        if (std::abs(along) <= 1e-12) return false;
        const double t = (line - (keepsY ? curve.from.y : curve.from.x)) / along;
        if (t <= 0.0 || t >= 1.0) return false;
        found.push_back({{curve.from.x + (curve.to.x - curve.from.x) * t, curve.from.y + (curve.to.y - curve.from.y) * t},
                         t * curve.length});
    } else {
        const double centreAcross = keepsY ? curve.centre.y : curve.centre.x;
        const double centreAlong = keepsY ? curve.centre.x : curve.centre.y;
        const double gap = line - centreAcross;
        const double inside = curve.radius * curve.radius - gap * gap;
        if (inside < 0.0) return false;
        const double root = std::sqrt(inside);
        for (double sign : {-1.0, 1.0}) {
            const double other = centreAlong + sign * root;
            const Vec2 candidate = keepsY ? Vec2{other, line} : Vec2{line, other};
            const CurveProjection projection = projectOntoCurve(candidate, curve);
            if (projection.distance > 1e-6) continue;
            found.push_back({candidate, projection.offset});
        }
    }
    bool any = false;
    double bestDistance = 0.0;
    for (const auto& [candidate, candidateOffset] : found) {
        if (candidateOffset < kMinSplitDistance || candidateOffset > curve.length - kMinSplitDistance) continue;
        const double d = distance(position, candidate);
        if (!any || d < bestDistance) {
            any = true;
            bestDistance = d;
            place = candidate;
            offset = candidateOffset;
        }
    }
    return any;
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

    // With orthogonalFrom the place on a wall is where the horizontal or vertical line crosses it, so Shift keeps the
    // wall straight.
    if (radius > 0.0 && options.toWalls) {
        const Wall* best = nullptr;
        Vec2 bestPosition;
        double bestOffset = 0.0;
        double bestDistance = radius;
        for (const Wall& w : floor.walls) {
            if (listed(options.ignorePoints, w.from) || listed(options.ignorePoints, w.to)) continue;
            const WallCurve curve = wallCurve(floor, w);
            if (!curve.valid) continue;
            Vec2 place;
            double offset = 0.0;
            if (options.orthogonalFrom) {
                const double line = keepsY ? options.orthogonalFrom->y : options.orthogonalFrom->x;
                if (!crossingNear(curve, keepsY, line, position, place, offset)) continue;
            } else {
                const CurveProjection projection = projectOntoCurve(position, curve);
                place = projection.position;
                offset = projection.offset;
            }
            if (offset < kMinSplitDistance || offset > curve.length - kMinSplitDistance) continue;
            const double d = distance(position, place);
            if (d <= bestDistance) {
                bestDistance = d;
                bestPosition = place;
                bestOffset = offset;
                best = &w;
            }
        }
        if (best) {
            result.position = bestPosition;
            result.kind = SnapKind::Wall;
            result.wallId = best->id;
            result.wallOffset = bestOffset;
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

    // An opening is under the position when its footprint (or a place within radius of it) is.
    const Opening* bestOpening = nullptr;
    double bestOpeningDistance = 0.0;
    double bestOpeningCentre = 0.0;
    for (const Opening& o : floor.openings) {
        const OpeningPlacement placement = openingPlacement(floor, o);
        if (!placement.valid || placement.footprint.empty()) continue;
        const double d = distanceToPolygon(position, placement.footprint);
        if (d > radius) continue;
        const double centre = distance(position, placement.centre);
        if (!bestOpening || d < bestOpeningDistance - 1e-12 || (std::fabs(d - bestOpeningDistance) <= 1e-12 && centre < bestOpeningCentre)) {
            bestOpening = &o;
            bestOpeningDistance = d;
            bestOpeningCentre = centre;
        }
    }
    if (bestOpening) return {PickKind::Opening, bestOpening->id};

    // An item is under the position when its footprint (or a place within radius of it) is. The last one in the
    // list is drawn on top, so it wins.
    const Item* topItem = nullptr;
    for (const Item& item : floor.items) {
        const ItemFootprint footprint = itemFootprint(item);
        const std::vector<Vec2> corners(footprint.corners.begin(), footprint.corners.end());
        if (insidePolygon(position, corners) || distanceToPolygon(position, corners) <= radius) topItem = &item;
    }
    if (topItem) return {PickKind::Item, topItem->id};

    // A wall is under the position when its band (or a place within radius of it) is. Where bands overlap, the wall
    // whose centre line is closest wins.
    const Wall* bestWall = nullptr;
    double bestWallDistance = 0.0;
    for (const Wall& w : floor.walls) {
        const std::vector<std::vector<Vec2>> band = wallBand(floor, w);
        double d = 1e300;
        for (const auto& outline : band) d = std::min(d, distanceToPolygon(position, outline));
        if (d > radius) continue;
        const double centreDistance = distanceToLine(position, wallPath(floor, w).centreLine);
        if (!bestWall || centreDistance < bestWallDistance) {
            bestWallDistance = centreDistance;
            bestWall = &w;
        }
    }
    if (bestWall) return {PickKind::Wall, bestWall->id};
    if (Id room = roomAt(floor, position); !room.empty()) return {PickKind::Room, std::move(room)};
    return {};
}

}
