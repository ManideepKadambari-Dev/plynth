#include "plynth/core/LightStrips.h"

#include "plynth/core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace plynth {

namespace {

// The first vector less the second.
Vec2 minus(Vec2 a, Vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}

// How much two directions point the same way.
double dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

// How much the second direction turns left from the first.
double cross(Vec2 a, Vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

// Says whether a number is finite and from low to high, with the small slack checkPlan allows.
bool within(double v, double low, double high)
{
    return std::isfinite(v) && v >= low - 1e-9 && v <= high + 1e-9;
}

}

// Checks a light strip against its floor: its line or its stair, its height and its power.
Status checkLightStrip(const LightStrip& strip, const Floor& floor)
{
    for (const PlanXY& p : strip.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return fail(ErrorCode::InvalidValue, "The light strip points must be finite numbers.");
        }
    }
    if (strip.kind == LightStripKind::Step) {
        if (!strip.stair) return fail(ErrorCode::InvalidValue, "A step light strip names the stair it lights.");
        if (!findStair(floor, *strip.stair)) {
            return fail(ErrorCode::NotFound, "There is no stair " + *strip.stair + " on the floor " + floor.id + ".");
        }
        if (!strip.points.empty()) {
            return fail(ErrorCode::InvalidValue, "A step light strip follows its stair and has no points of its own.");
        }
    } else {
        if (strip.stair) return fail(ErrorCode::InvalidValue, "Only a step light strip names a stair.");
        if (strip.points.size() < 2) return fail(ErrorCode::InvalidValue, "A light strip needs at least two points.");
        double length = 0.0;
        for (std::size_t i = 1; i < strip.points.size(); ++i) {
            const Vec2 a{strip.points[i - 1].x, strip.points[i - 1].y};
            const Vec2 b{strip.points[i].x, strip.points[i].y};
            const double piece = distance(a, b);
            if (piece < kMinStripPiece) {
                return fail(ErrorCode::InvalidValue, "Two points in a row of a light strip must be at least 0.01 metres apart.");
            }
            length += piece;
        }
        if (length < kMinStripLength - 1e-9) {
            return fail(ErrorCode::InvalidValue, "A light strip must be at least 0.1 metres long.");
        }
    }
    if (!within(strip.z, 0.0, floor.ceilingHeight)) {
        return fail(ErrorCode::InvalidValue, "The light strip height must be from 0 to the ceiling height of the floor.");
    }
    if (!within(strip.power, 0.0, 200.0)) {
        return fail(ErrorCode::InvalidValue, "The light strip power must be from 0 to 200 watts per metre.");
    }
    return {};
}

// Finds the nose of a step: the first edge of its outline the walk line crosses on its way in.
std::optional<StepNose> stepNose(const std::vector<Vec2>& outline, const std::vector<Vec2>& walkLine)
{
    const std::size_t n = outline.size();
    if (n < 3) return std::nullopt;
    Vec2 centre;
    for (const Vec2& p : outline) centre = {centre.x + p.x / static_cast<double>(n), centre.y + p.y / static_cast<double>(n)};
    const auto outward = [&](std::size_t i) {
        const Vec2 a = outline[i];
        const Vec2 b = outline[(i + 1) % n];
        const Vec2 d = minus(b, a);
        const double length = std::hypot(d.x, d.y);
        Vec2 normal{-d.y / length, d.x / length};
        const Vec2 middle{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
        if (dot(minus(middle, centre), normal) < 0.0) normal = {-normal.x, -normal.y};
        return normal;
    };

    double bestAt = std::numeric_limits<double>::infinity();
    std::size_t best = n;
    double travelled = 0.0;
    for (std::size_t k = 1; k < walkLine.size(); ++k) {
        const Vec2 s = walkLine[k - 1];
        const Vec2 sd = minus(walkLine[k], s);
        const double walkLength = std::hypot(sd.x, sd.y);
        if (walkLength < 1e-12) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 a = outline[i];
            const Vec2 ed = minus(outline[(i + 1) % n], a);
            if (std::hypot(ed.x, ed.y) < 1e-6) continue;
            const double denom = cross(sd, ed);
            if (std::fabs(denom) < 1e-12) continue;
            const Vec2 as = minus(a, s);
            const double t = cross(as, ed) / denom;
            const double u = cross(as, sd) / denom;
            if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9) continue;
            // Crossing an edge on the way out of the step is not its nose.
            if (dot(sd, outward(i)) >= 0.0) continue;
            const double reached = travelled + t * walkLength;
            if (reached < bestAt - 1e-12) {
                bestAt = reached;
                best = i;
            }
        }
        travelled += walkLength;
    }
    if (best == n) return std::nullopt;
    return StepNose{outline[best], outline[(best + 1) % n], outward(best)};
}

// The starting height for a new strip of a kind.
double defaultStripHeight(LightStripKind kind, double ceilingHeight)
{
    switch (kind) {
    case LightStripKind::Cove: return std::max(0.0, ceilingHeight - 0.05);
    case LightStripKind::Soffit: return ceilingHeight;
    case LightStripKind::Edge:
    case LightStripKind::Step: return 0.0;
    }
    return 0.0;
}

}
