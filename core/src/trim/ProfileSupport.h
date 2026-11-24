#pragma once

#include "plynth/core/TrimProfiles.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace plynth::profiles {

inline constexpr double kPi = 3.14159265358979323846;

// The common part of the built-in profiles: a fixed id, name and sort key.
class BuiltInProfile : public TrimProfile {
public:
    BuiltInProfile(std::string_view id, std::string_view name, int key) : id_(id), name_(name), key_(key) {}

    std::string_view id() const override { return id_; }
    std::string_view name() const override { return name_; }
    int sortKey() const override { return key_; }
    bool builtIn() const override { return true; }

private:
    std::string_view id_;
    std::string_view name_;
    int key_;
};

// Adds one point unless it equals the last one already in the list.
inline void appendPoint(std::vector<Vec2>& out, Vec2 p)
{
    if (!out.empty() && std::fabs(out.back().x - p.x) < 1e-12 && std::fabs(out.back().y - p.y) < 1e-12) return;
    out.push_back(p);
}

// Adds the points of a quarter or other part of an ellipse from one angle to another, both ends included, cut
// into n segments.
inline void appendEllipseArc(std::vector<Vec2>& out, Vec2 centre, double rx, double ry, double fromAngle,
                             double toAngle, int n)
{
    for (int k = 0; k <= n; ++k) {
        const double a = fromAngle + (toAngle - fromAngle) * k / n;
        appendPoint(out, {centre.x + rx * std::cos(a), centre.y + ry * std::sin(a)});
    }
}

// How many segments an elliptic arc gets: as many as a circle of its larger radius.
inline int ellipseSegments(double rx, double ry, double sweep)
{
    return arcSegmentCount(std::max(rx, ry), sweep);
}

// Keeps every point inside the box, which removes rounding errors in the last digit, and drops a closing point
// that equals the first.
inline void finishOutline(std::vector<Vec2>& out, double height, double depth)
{
    for (Vec2& p : out) {
        p.x = std::clamp(p.x, 0.0, depth);
        p.y = std::clamp(p.y, 0.0, height);
    }
    while (out.size() > 1 && std::fabs(out.front().x - out.back().x) < 1e-12 && std::fabs(out.front().y - out.back().y) < 1e-12) {
        out.pop_back();
    }
}

}
