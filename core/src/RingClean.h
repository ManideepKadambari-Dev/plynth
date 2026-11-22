#pragma once

#include "plynth/core/Geometry.h"

#include <cmath>
#include <cstddef>
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

// The ring without spikes: a point where it runs along a line and straight back over it. Rounding in the union
// can leave one where two bands of different heights meet at a diagonal, with no width and no area. Taking one
// out can leave its two neighbours on top of each other, so this alone does not leave a clean ring.
inline std::vector<Vec2> withoutSpikes(std::vector<Vec2> ring)
{
    bool changed = true;
    while (changed && ring.size() > 3) {
        changed = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2 a = ring[(i + ring.size() - 1) % ring.size()];
            const Vec2 b = ring[i];
            const Vec2 c = ring[(i + 1) % ring.size()];
            const double ab = distance(a, b);
            const Vec2 u{b.x - a.x, b.y - a.y};
            const Vec2 v{c.x - b.x, c.y - b.y};
            if (ab < kRingClose || (u.x * v.x + u.y * v.y) >= 0.0) continue;
            if (std::fabs(u.x * v.y - u.y * v.x) / ab < kRingClose) {
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return ring;
}

// A ring from a union with repeated points and spikes taken out until neither is left: no edge shorter than
// kRingClose and no spike.
inline std::vector<Vec2> cleanedWallRing(const std::vector<Vec2>& input)
{
    std::vector<Vec2> ring = input;
    for (;;) {
        const std::size_t before = ring.size();
        ring = withoutRepeats(ring);
        ring = withoutSpikes(std::move(ring));
        if (ring.size() == before) return ring;
    }
}

}
