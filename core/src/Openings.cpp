#include "plynth/core/Openings.h"

#include "BandOutline.h"

#include "plynth/core/OpeningShapes.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace plynth {

using detail::bandAround;
using detail::BandEnd;
using detail::distanceToLine;
using detail::distanceToPolygon;

namespace {

// Writes a length in metres with two decimals for a message.
std::string metres(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.2f m", value);
    return text;
}

// The clearance a wall needs at one of its ends: the larger of the trim of a rounded corner and half the
// thickness of the thickest other wall at the point that does not continue in line.
double clearanceAt(const Floor& floor, const Wall& wall, bool atStart, double trim)
{
    const Id& pointId = atStart ? wall.from : wall.to;
    const WallCurve own = wallCurve(floor, wall);
    // The way this wall leaves the point, into the wall.
    const Vec2 leaving = atStart ? tangentAlong(own, 0.0)
                                 : Vec2{-tangentAlong(own, own.length).x, -tangentAlong(own, own.length).y};
    double clearance = trim;
    for (const Wall& other : floor.walls) {
        if (other.id == wall.id) continue;
        const bool otherStarts = other.from == pointId;
        if (!otherStarts && other.to != pointId) continue;
        const WallCurve curve = wallCurve(floor, other);
        if (!curve.valid) continue;
        Vec2 away = tangentAlong(curve, otherStarts ? 0.0 : curve.length);
        if (!otherStarts) away = {-away.x, -away.y};
        // Straight on is the opposite of the direction this wall leaves in.
        const double cosAngle = std::clamp(-(leaving.x * away.x + leaving.y * away.y), -1.0, 1.0);
        if (std::acos(cosAngle) <= kInLineAngle) continue;
        clearance = std::max(clearance, other.thickness / 2.0);
    }
    return clearance;
}

}

// The free part of a wall.
WallSpan wallSpan(const Floor& floor, const Wall& wall)
{
    WallSpan span;
    span.height = wallHeight(floor, wall);
    const WallPath path = wallPath(floor, wall);
    if (!path.curve.valid) return span;
    span.start = clearanceAt(floor, wall, true, path.startTrim);
    span.end = path.curve.length - clearanceAt(floor, wall, false, path.endTrim);
    return span;
}

// The sizes a new opening of a type gets.
Opening defaultOpening(const DefaultSizes& sizes, OpeningType type)
{
    Opening o;
    o.type = type;
    if (type == OpeningType::Window) {
        o.width = sizes.windowWidth;
        o.height = sizes.windowHeight;
        o.sill = sizes.windowSill;
    } else {
        o.width = sizes.doorWidth;
        o.height = sizes.doorHeight;
        o.sill = 0.0;
    }
    return o;
}

// Checks the opening against the rules listed in Openings.h.
Status checkOpeningFit(const Floor& floor, const Opening& opening)
{
    const Wall* wall = findWall(floor, opening.wall);
    if (!wall) return fail(ErrorCode::NotFound, "There is no wall " + opening.wall + " on this floor.");

    if (!std::isfinite(opening.offset) || !std::isfinite(opening.width) || !std::isfinite(opening.height)
        || !std::isfinite(opening.sill)) {
        return fail(ErrorCode::InvalidValue, "The offset, width, height and sill of an opening must be finite numbers.");
    }
    if (!(opening.width > 0.0) || !(opening.height > 0.0)) {
        return fail(ErrorCode::InvalidValue, "The width and height of an opening must be above zero.");
    }
    if (opening.sill < 0.0) return fail(ErrorCode::InvalidValue, "The sill of an opening must be zero or more.");
    if (Status s = checkOpeningShape(opening.shape, opening.width, opening.height); !s.ok()) return s;

    const WallSpan span = wallSpan(floor, *wall);
    const double left = opening.offset - opening.width / 2.0;
    const double right = opening.offset + opening.width / 2.0;
    if (left < span.start - kOpeningTouchTolerance) {
        return fail(ErrorCode::InvalidValue,
                    "The opening reaches " + metres(span.start - left) + " past the free part of wall " + wall->id
                        + " at its start, which begins " + metres(span.start) + " from the wall's end.");
    }
    if (right > span.end + kOpeningTouchTolerance) {
        return fail(ErrorCode::InvalidValue,
                    "The opening reaches " + metres(right - span.end) + " past the free part of wall " + wall->id
                        + " at its end, which stops " + metres(wallLength(floor, *wall) - span.end) + " before the wall's end.");
    }
    if (opening.sill + opening.height > span.height + kOpeningTouchTolerance) {
        return fail(ErrorCode::InvalidValue,
                    "The opening reaches " + metres(opening.sill + opening.height - span.height) + " above the top of wall "
                        + wall->id + ", which is " + metres(span.height) + " high.");
    }
    for (const Opening& other : floor.openings) {
        if (other.id == opening.id || other.wall != opening.wall) continue;
        const double overlapU = std::min(right, other.offset + other.width / 2.0) - std::max(left, other.offset - other.width / 2.0);
        const double overlapV = std::min(opening.sill + opening.height, other.sill + other.height) - std::max(opening.sill, other.sill);
        if (overlapU > kOpeningTouchTolerance && overlapV > kOpeningTouchTolerance) {
            return fail(ErrorCode::InvalidValue, "The opening overlaps " + other.id + " on wall " + wall->id + ".");
        }
    }
    return {};
}

// Lists the openings that checkOpeningFit refuses.
std::vector<OpeningProblem> openingProblems(const Floor& floor)
{
    std::vector<OpeningProblem> problems;
    for (const Opening& o : floor.openings) {
        Status s = checkOpeningFit(floor, o);
        if (!s.ok()) problems.push_back({o.id, std::move(s)});
    }
    return problems;
}

// Moves and sizes the opening so it sits inside its wall.
Opening fitOpeningBox(const Floor& floor, Opening opening)
{
    const Wall* wall = findWall(floor, opening.wall);
    if (!wall) return opening;
    const WallSpan span = wallSpan(floor, *wall);
    const double low = span.start + opening.width / 2.0;
    const double high = span.end - opening.width / 2.0;
    opening.offset = low > high ? (span.start + span.end) / 2.0 : std::clamp(opening.offset, low, high);
    const double highest = span.height - opening.height;
    opening.sill = std::clamp(opening.sill, 0.0, std::max(0.0, highest));
    return opening;
}

// Works out where an opening is on the plan.
OpeningPlacement openingPlacement(const Floor& floor, const Opening& opening)
{
    OpeningPlacement placement;
    const Wall* wall = findWall(floor, opening.wall);
    if (!wall) return placement;
    const WallCurve curve = wallCurve(floor, *wall);
    if (!curve.valid) return placement;

    const double from = std::clamp(opening.offset - opening.width / 2.0, 0.0, curve.length);
    const double to = std::clamp(opening.offset + opening.width / 2.0, 0.0, curve.length);
    placement.valid = true;
    placement.centre = pointAlong(curve, opening.offset);
    placement.start = pointAlong(curve, from);
    placement.end = pointAlong(curve, to);
    placement.startTangent = tangentAlong(curve, from);
    placement.endTangent = tangentAlong(curve, to);
    placement.thickness = wall->thickness;
    placement.centreLine = curvePoints(curve, from, to);
    const double half = wall->thickness / 2.0;
    if (placement.centreLine.size() >= 2 && distance(placement.centreLine.front(), placement.centreLine.back()) > 0.0) {
        const BandEnd first{placement.centreLine.front(), placement.startTangent};
        const BandEnd last{placement.centreLine.back(), placement.endTangent};
        placement.footprint = bandAround(placement.centreLine, half, first, last);
    }
    return placement;
}

// Finds the wall and the place along it for a position.
OpeningSpot placeOpeningAt(const Floor& floor, Vec2 position, double radius, double width, double step)
{
    OpeningSpot spot;
    if (!std::isfinite(width) || !(width > 0.0)) {
        spot.status = fail(ErrorCode::InvalidValue, "The opening width must be a number above zero.");
        return spot;
    }
    radius = std::max(radius, 0.0);

    const Wall* best = nullptr;
    double bestBand = 0.0;
    double bestCentre = 0.0;
    for (const Wall& w : floor.walls) {
        double band = 1e300;
        for (const auto& outline : wallBand(floor, w)) band = std::min(band, distanceToPolygon(position, outline));
        if (band > radius) continue;
        const double centre = distanceToLine(position, wallPath(floor, w).centreLine);
        if (!best || band < bestBand - 1e-12 || (std::fabs(band - bestBand) <= 1e-12 && centre < bestCentre)) {
            best = &w;
            bestBand = band;
            bestCentre = centre;
        }
    }
    if (!best) {
        spot.status = fail(ErrorCode::NotFound, "There is no wall here.");
        return spot;
    }

    const WallCurve curve = wallCurve(floor, *best);
    double offset = projectOntoCurve(position, curve).offset;
    if (step > 0.0 && std::isfinite(step)) offset = std::round(offset / step) * step + 0.0;
    const WallSpan span = wallSpan(floor, *best);
    const double free = span.end - span.start;
    if (free < width - kOpeningTouchTolerance) {
        spot.status = fail(ErrorCode::InvalidValue, "The free part of wall " + best->id + " is only " + metres(std::max(free, 0.0))
                                                         + " long, and the opening is " + metres(width) + " wide.");
        return spot;
    }
    spot.wall = best->id;
    spot.offset = std::clamp(offset, span.start + width / 2.0, span.end - width / 2.0);
    return spot;
}

}
