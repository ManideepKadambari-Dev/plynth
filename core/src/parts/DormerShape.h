#pragma once

#include "plynth/core/BuildGeometry.h"
#include "plynth/core/Dormers.h"
#include "plynth/core/Roofs.h"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

// How a dormer stands on its roof (Dormers.h), worked out once for the checks, the commands and the part maker.
// Local points are (u, t): u across the front from its middle, positive to the right seen from in front, and t
// back up the slope from the front, both in metres on the plan. Heights are world Z.
namespace plynth::dormers {

// The front reaches this far above the window's head, metres.
inline constexpr double kWindowHead = 0.1;
// The front keeps at least this much on each side of the window, metres.
inline constexpr double kWindowJamb = 0.05;

enum class Kind { Gable, Shed };

// One dormer on its roof.
struct Shape {
    std::string code; // empty when it can be built, otherwise its problem code
    Status status;    // the problem as InvalidValue
    Kind kind = Kind::Gable;
    int roofIndex = 0;
    Vec2 front;               // world X and Y of the middle of the front's foot
    Vec2 back;                // unit world direction up the slope
    Vec2 across;              // unit world direction of u
    double slope = 0.0;       // the main roof's rise per metre back
    double footZ = 0.0;       // the main roof's top at the front
    double frontHeight = 0.0; // from footZ up to the top of the cheeks
    double rise = 0.0;        // the rise per metre of the dormer's own roof
    double width = 0.0;
    double depth = 0.0;
    std::vector<Vec2> footprint;           // local, anticlockwise: where the dormer stands in the roof
    std::vector<std::vector<Vec2>> slabs;  // local, anticlockwise: its own roof seen from above, one per plane

    // The world X and Y of a local point.
    Vec2 at(Vec2 local) const;
    // The same for a ring.
    std::vector<Vec2> atRing(const std::vector<Vec2>& ring) const;
    // The main roof's top and its underside at a local point.
    double covering(Vec2 local) const;
    double underside(Vec2 local) const;
    // The underside of the dormer's own roof over a local point (the slab that holds it, for a gable by its side).
    double roofUnder(Vec2 local) const;
};

// Says whether a dormer type is one Plynth builds: "gable" or "shed".
bool knownType(std::string_view type);

// Works out a dormer of the floor from that floor's roofs (floorRoofs) and roof sections (roofAreasOf).
Shape shapeOf(const Dormer& dormer, const Id& floorId, const std::vector<RoofBuild>& roofs,
              const std::vector<RoofArea>& areas);

// Says whether two dormers that can be built share more than a sliver, their own roofs included.
bool overlaps(const Shape& a, const Shape& b);

// The part of a convex local ring where value(p) is zero or more; value must be affine over the ring.
template <typename Value>
std::vector<Vec2> clippedRing(const std::vector<Vec2>& ring, const Value& value)
{
    std::vector<Vec2> kept;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % ring.size()];
        const double va = value(a);
        const double vb = value(b);
        if (va >= 0.0) kept.push_back(a);
        if ((va > 0.0 && vb < 0.0) || (va < 0.0 && vb > 0.0)) {
            const double f = va / (va - vb);
            kept.push_back({a.x + f * (b.x - a.x), a.y + f * (b.y - a.y)});
        }
    }
    std::vector<Vec2> out;
    for (const Vec2& p : kept) {
        const bool same = !out.empty() && std::abs(p.x - out.back().x) < 1e-9 && std::abs(p.y - out.back().y) < 1e-9;
        if (!same) out.push_back(p);
    }
    while (out.size() > 1 && std::abs(out.front().x - out.back().x) < 1e-9 && std::abs(out.front().y - out.back().y) < 1e-9) {
        out.pop_back();
    }
    return out;
}

// The area of a ring, positive when it turns anticlockwise.
double ringArea(const std::vector<Vec2>& ring);

}
