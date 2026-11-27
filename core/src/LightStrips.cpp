#include "plynth/core/LightStrips.h"

#include "plynth/core/Geometry.h"

#include <algorithm>
#include <cmath>

namespace plynth {

namespace {

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
