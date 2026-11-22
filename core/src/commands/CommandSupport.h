#pragma once

#include "plynth/core/Plan.h"
#include "plynth/core/WallGeometry.h"

#include <cmath>

namespace plynth::detail {

// Finds the floor that has a point with this id. Null when there is none.
inline Floor* floorOfPoint(Plan& plan, std::string_view pointId)
{
    for (auto& f : plan.floors) {
        if (findPoint(f, pointId)) return &f;
    }
    return nullptr;
}

// Finds the floor that has a wall with this id. Null when there is none.
inline Floor* floorOfWall(Plan& plan, std::string_view wallId)
{
    for (auto& f : plan.floors) {
        if (findWall(f, wallId)) return &f;
    }
    return nullptr;
}

// Checks that a wall with these end positions, bulge and thickness is still a real arc: not bent past a half
// circle and with a radius above half the thickness. A straight wall always passes.
inline Status checkCurveLimits(Vec2 from, Vec2 to, double bulge, double thickness, std::string_view wallId)
{
    if (std::fabs(bulge) < kMinBulge) return {};
    const double chord = distance(from, to);
    if (!(chord >= 1e-9)) return {};
    const double size = std::fabs(bulge);
    if (size > kMaxBulgeRatio * chord + 1e-9) {
        return fail(ErrorCode::InvalidValue, "The wall " + std::string(wallId)
                        + " is curved too much for this length. The most is a half circle.");
    }
    const double radius = (chord * chord / 4.0 + size * size) / (2.0 * size);
    if (!(radius > thickness / 2.0)) {
        return fail(ErrorCode::InvalidValue, "The wall " + std::string(wallId) + " is bent too tightly for its thickness.");
    }
    return {};
}

// Says whether a number is finite and above zero.
inline bool isPositive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

// Says whether text is a well formed catalog name: not empty, only letters, digits, "_" and "-".
inline bool isWellFormedName(std::string_view name)
{
    if (name.empty()) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Says whether text is a well formed material name: the catalog name rule with at most 128 characters.
inline bool isMaterialName(std::string_view name)
{
    return name.size() <= 128 && isWellFormedName(name);
}

}
