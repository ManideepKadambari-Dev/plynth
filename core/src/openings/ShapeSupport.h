#pragma once

#include "plynth/core/OpeningShapes.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace plynth::shapes {

inline constexpr double kPi = 3.14159265358979323846;

// The common part of the built-in shapes: a fixed id, name, size rule and sort key.
class BuiltInShape : public OpeningShape {
public:
    BuiltInShape(std::string_view id, std::string_view name, std::string_view rule, int key)
        : id_(id), name_(name), rule_(rule), key_(key)
    {
    }

    std::string_view id() const override { return id_; }
    std::string_view name() const override { return name_; }
    std::string_view sizeRule() const override { return rule_; }
    int sortKey() const override { return key_; }
    bool builtIn() const override { return true; }

private:
    std::string_view id_;
    std::string_view name_;
    std::string_view rule_;
    int key_;
};

// Rounds a segment count up to an even number, so an arc that is symmetric has a point at its middle.
inline int evenCount(int n)
{
    return n % 2 == 0 ? n : n + 1;
}

// Adds one point unless it equals the last one already in the list.
inline void appendPoint(std::vector<Vec2>& out, Vec2 p)
{
    if (!out.empty() && std::fabs(out.back().x - p.x) < 1e-12 && std::fabs(out.back().y - p.y) < 1e-12) return;
    out.push_back(p);
}

// Adds the points of an arc of a circle from one angle to another, both ends included, cut into n segments.
inline void appendArc(std::vector<Vec2>& out, Vec2 centre, double radius, double fromAngle, double toAngle, int n)
{
    for (int k = 0; k <= n; ++k) {
        const double a = fromAngle + (toAngle - fromAngle) * k / n;
        appendPoint(out, {centre.x + radius * std::cos(a), centre.y + radius * std::sin(a)});
    }
}

// Keeps every point inside the box, which removes rounding errors in the last digit, and drops a closing
// point that equals the first.
inline void finishOutline(std::vector<Vec2>& out, double width, double height)
{
    for (Vec2& p : out) {
        p.x = std::clamp(p.x, -width / 2.0, width / 2.0);
        p.y = std::clamp(p.y, 0.0, height);
    }
    while (out.size() > 1 && std::fabs(out.front().x - out.back().x) < 1e-12 && std::fabs(out.front().y - out.back().y) < 1e-12) {
        out.pop_back();
    }
}

}
