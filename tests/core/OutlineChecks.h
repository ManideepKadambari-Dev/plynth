#pragma once

#include <plynth/core/Geometry.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace plynth::test {

// Says whether two segments cross or touch, other than neighbours meeting at their shared corner.
inline bool segmentsMeet(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const auto cross = [](Vec2 p, Vec2 q, Vec2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
    const auto sign = [](double v) { return v > 1e-12 ? 1 : (v < -1e-12 ? -1 : 0); };
    const int d1 = sign(cross(a, b, c));
    const int d2 = sign(cross(a, b, d));
    const int d3 = sign(cross(c, d, a));
    const int d4 = sign(cross(c, d, b));
    return d1 * d2 < 0 && d3 * d4 < 0;
}

// Says whether a closed polygon crosses itself, comparing edges that do not share a corner.
inline bool crossesItself(const std::vector<Vec2>& p)
{
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            if (segmentsMeet(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return true;
        }
    }
    return false;
}

// Checks an opening outline: anticlockwise with v up, no crossing edges, no repeated points, inside the box
// of this width and height, and touching all four sides.
inline void requireCleanOutline(const std::vector<Vec2>& o, double width, double height)
{
    constexpr double eps = 1e-9;
    REQUIRE(signedArea(o) > 0.0);
    REQUIRE_FALSE(crossesItself(o));
    double minU = 1e9, maxU = -1e9, minV = 1e9, maxV = -1e9;
    for (std::size_t i = 0; i < o.size(); ++i) {
        minU = std::min(minU, o[i].x);
        maxU = std::max(maxU, o[i].x);
        minV = std::min(minV, o[i].y);
        maxV = std::max(maxV, o[i].y);
        REQUIRE(distance(o[i], o[(i + 1) % o.size()]) > 1e-9);
    }
    REQUIRE(minU >= -width / 2.0 - eps);
    REQUIRE(maxU <= width / 2.0 + eps);
    REQUIRE(minV >= -eps);
    REQUIRE(maxV <= height + eps);
    REQUIRE(std::fabs(minU + width / 2.0) < eps);
    REQUIRE(std::fabs(maxU - width / 2.0) < eps);
    REQUIRE(std::fabs(minV) < eps);
    REQUIRE(std::fabs(maxV - height) < eps);
}

}
