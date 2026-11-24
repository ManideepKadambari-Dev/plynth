#pragma once

#include "plynth/core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <vector>

namespace plynth::detail {

// Points closer than this are the same point.
inline constexpr double kRingClose = 1e-6;

// The ring without points that sit on the one before them.
inline std::vector<Vec2> withoutRepeats(const std::vector<Vec2>& ring)
{
    std::vector<Vec2> out;
    for (const Vec2& p : ring) {
        if (out.empty() || distance(out.back(), p) >= kRingClose) out.push_back(p);
    }
    while (out.size() > 1 && distance(out.front(), out.back()) < kRingClose) out.pop_back();
    return out;
}

// How wide the turn at b is: the smaller of how far c is from line ab and how far a is from line bc. It is the
// same when the ring runs the other way.
inline double turnWidth(Vec2 a, Vec2 b, Vec2 c)
{
    const double longer = (std::max)(distance(a, b), distance(b, c));
    if (longer <= 0.0) return 0.0;
    return std::fabs((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x)) / longer;
}

// Whether b can go: it sits on a neighbour, or the ring runs back over itself there in a spike too thin to keep.
inline bool canGo(Vec2 a, Vec2 b, Vec2 c)
{
    if (distance(a, b) < kRingClose || distance(b, c) < kRingClose) return true;
    const bool back = (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) < 0.0;
    return back && turnWidth(a, b, c) < kRingClose;
}

// A ring from a union with no edge shorter than kRingClose and no spike, or fewer than three points when the
// ring was nothing more than that. Rounding in the union can leave a spike where bands of different heights
// meet. The narrowest point goes first, so a corner outlasts a point beside it. Equally narrow ones go by x and
// then y, so neither the first point nor the direction of the ring picks the point.
inline std::vector<Vec2> cleanedWallRing(const std::vector<Vec2>& input)
{
    std::vector<Vec2> ring = input;
    while (ring.size() >= 3) {
        const std::size_t n = ring.size();
        std::size_t next = n;
        std::tuple<double, double, double> least;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 a = ring[(i + n - 1) % n];
            const Vec2 b = ring[i];
            const Vec2 c = ring[(i + 1) % n];
            if (!canGo(a, b, c)) continue;
            const std::tuple<double, double, double> order{turnWidth(a, b, c), b.x, b.y};
            if (next < n && order >= least) continue;
            next = i;
            least = order;
        }
        if (next == n) break;
        ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(next));
    }
    return ring;
}

}
