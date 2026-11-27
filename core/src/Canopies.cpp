#include "plynth/core/Canopies.h"

#include "commands/CommandSupport.h"
#include "parts/PartGeometry.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace plynth {

namespace {

// Says whether a number is finite and from low to high, allowing for rounding.
bool inRange(double v, double low, double high)
{
    return std::isfinite(v) && v >= low - 1e-9 && v <= high + 1e-9;
}

// Checks a material name when one is set.
Status checkMaterial(const std::optional<std::string>& material)
{
    if (material && !detail::isMaterialName(*material)) {
        return fail(ErrorCode::InvalidValue, "The material \"" + *material + "\" is not a valid name.");
    }
    return {};
}

}

// Checks the values a canopy command would set.
Status checkCanopy(const Canopy& canopy)
{
    for (const PlanXY& p : canopy.outline) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return fail(ErrorCode::InvalidValue, "The canopy outline must be finite numbers.");
        }
    }
    if (canopy.outline.size() < 3) return fail(ErrorCode::InvalidValue, "An outline needs at least three points.");
    const std::vector<Vec2> points = partgeometry::pointsOf(canopy.outline);
    if (!(std::fabs(ringAreaOf(points)) > 0.01)) {
        return fail(ErrorCode::InvalidValue, "The outline must enclose more than 0.01 square metres.");
    }
    if (partgeometry::crossesItself(points)) return fail(ErrorCode::InvalidValue, "The canopy outline crosses itself.");
    if (!inRange(canopy.height, 1.0, 20.0)) {
        return fail(ErrorCode::InvalidValue, "The canopy height must be from 1 to 20 metres.");
    }
    if (!inRange(canopy.thickness, 0.05, 1.0)) {
        return fail(ErrorCode::InvalidValue, "The canopy thickness must be from 0.05 to 1 metre.");
    }
    if (const Status s = checkRoofSettings(canopy.roof); !s.ok()) return s;
    if (const Status s = checkMaterial(canopy.material); !s.ok()) return s;
    if (const Status s = checkMaterial(canopy.undersideMaterial); !s.ok()) return s;
    return checkMaterial(canopy.fasciaMaterial);
}

}
