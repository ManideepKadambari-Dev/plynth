#pragma once

#include "plynth/core/Plan.h"

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

}
