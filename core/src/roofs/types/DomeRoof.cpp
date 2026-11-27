#include "plynth/core/Geometry.h"
#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"
#include "roofs/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace plynth {

namespace {

// The middle of a ring by area, measured from its first point so a place far from the origin loses no digits.
Vec2 middleOf(const std::vector<Vec2>& ring)
{
    const Vec2 o = ring.front();
    double twice = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) {
        const Vec2 a{ring[i].x - o.x, ring[i].y - o.y};
        const Vec2 b{ring[i + 1].x - o.x, ring[i + 1].y - o.y};
        const double c = a.x * b.y - a.y * b.x;
        twice += c;
        sx += (a.x + b.x) * c;
        sy += (a.y + b.y) * c;
    }
    if (std::fabs(twice) < 1e-18) return o;
    return {o.x + sx / (3.0 * twice), o.y + sy / (3.0 * twice)};
}

// The outline of a dome and how far its middle lies inside each edge.
struct DomeBase {
    std::vector<Vec2> ring;
    Vec2 middle;
    std::vector<Vec2> normals;   // inward unit normal of each edge
    std::vector<double> offsets; // the distance inside edge i is normals[i] . p - offsets[i]
    std::vector<double> depths;  // that distance for the middle
    double nearest = 0.0;        // the smallest depth
};

// Works out the dome's outline from a convex section.
DomeBase domeBaseOf(const WorldPolygon& section)
{
    DomeBase out;
    out.ring = roofskeleton::withoutStraightPoints(section.outline);
    if (out.ring.size() < 3) return out;
    out.middle = middleOf(out.ring);
    out.nearest = 1e18;
    for (std::size_t i = 0; i < out.ring.size(); ++i) {
        const Vec2 a = out.ring[i];
        const Vec2 b = out.ring[(i + 1) % out.ring.size()];
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        const Vec2 n = roofparts::leftOf({(b.x - a.x) / len, (b.y - a.y) / len});
        out.normals.push_back(n);
        out.offsets.push_back(n.x * a.x + n.y * a.y);
        out.depths.push_back(n.x * out.middle.x + n.y * out.middle.y - out.offsets.back());
        out.nearest = std::min(out.nearest, out.depths.back());
    }
    return out;
}

// The highest rise a dome over this base can have before it meets the edge nearest the middle steeper than
// kMaxRoofPitch.
double highestRise(const DomeBase& base)
{
    return base.nearest * std::tan(kMaxRoofPitch / 2.0 * std::numbers::pi / 180.0);
}

// A dome over a convex section: rings shrunk toward the section's middle, each a little higher, so every face
// between two rings is flat. On the edge nearest the middle the rings follow a circle that stands on the outline at
// base and is rise higher at the middle; the other edges share the rings' heights, so their faces are flatter.
// The rings are cut by arcSegmentCount, and the outer faces carry on over the eaves. Sections with holes or inward
// corners, and a rise that would meet the walls steeper than kMaxRoofPitch, are refused.
class DomeRoof final : public RoofType {
public:
    std::string_view id() const override { return "dome"; }
    std::string_view name() const override { return "Dome"; }
    int sortKey() const override { return 82; }
    bool usesPitch() const override { return false; }
    bool usesDirection() const override { return false; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesRise() const override { return true; }
    std::string_view fallback() const override { return kFlatRoofId; }

    Status fits(const RoofInput& input) const override
    {
        const DomeBase base = domeBaseOf(input.section);
        if (!input.section.holes.empty() || base.ring.size() < 3 || !roofskeleton::convexRing(base.ring) ||
            !(base.nearest > 1e-6)) {
            return fail(ErrorCode::InvalidValue,
                        "A dome needs a section without holes or inward corners, so it is built flat.");
        }
        const double highest = highestRise(base);
        if (input.settings.rise <= highest + 1e-9) return {};
        char text[32];
        std::snprintf(text, sizeof text, "%.2f", highest);
        return fail(ErrorCode::InvalidValue, "A dome on this section can rise at most " + std::string(text) +
                                                 " m before it meets the walls too steeply, so it is built flat.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const DomeBase dome = domeBaseOf(input.section);
        const std::size_t count = dome.ring.size();
        const double rise = input.settings.rise;
        if (count < 3 || !(dome.nearest > 1e-6) || !(rise > 0.0)) return {};
        const Vec2 c = dome.middle;
        const double radius = (dome.nearest * dome.nearest + rise * rise) / (2.0 * rise);
        const double sweep = std::asin(std::min(1.0, dome.nearest / radius));
        const int rings = arcSegmentCount(radius, sweep);

        // Ring k lies at share[k] of the way from the middle to the outline, height[k] high; ring 0 is the outline.
        std::vector<double> share(rings + 1);
        std::vector<double> height(rings + 1);
        for (int k = 0; k <= rings; ++k) {
            const double angle = sweep * (1.0 - static_cast<double>(k) / rings);
            share[k] = radius * std::sin(angle) / dome.nearest;
            height[k] = input.base + radius * (std::cos(angle) - std::cos(sweep));
        }
        share.front() = 1.0;
        share.back() = 0.0;
        height.front() = input.base;

        // planes[i * rings + k] is the face of edge i between rings k and k + 1, as height over the distance
        // inside the edge.
        std::vector<roofparts::Plane> planes;
        std::vector<roofparts::Piece> pieces;
        std::vector<roofparts::BreakLine> breaks;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec2 a = dome.ring[i];
            const Vec2 b = dome.ring[(i + 1) % count];
            const Vec2 n = dome.normals[i];
            const double off = dome.offsets[i];
            const double depth = dome.depths[i];
            const Vec2 toA{a.x - c.x, a.y - c.y};
            const Vec2 toB{b.x - c.x, b.y - c.y};
            breaks.push_back({c, toA});
            std::vector<WorldPolygon> wedge = roofparts::keepHalf(input.eaves, c, roofparts::leftOf(toA));
            if (!wedge.empty()) wedge = roofparts::keepHalf(wedge, c, {toB.y, -toB.x});
            for (int k = 0; k < rings; ++k) {
                const double from = (1.0 - share[k]) * depth;
                const double to = (1.0 - share[k + 1]) * depth;
                const double m = (height[k + 1] - height[k]) / (to - from);
                const roofparts::Plane plane{m * n.x, m * n.y, height[k] - m * (off + from)};
                planes.push_back(plane);
                if (k > 0) breaks.push_back({{n.x * (off + from), n.y * (off + from)}, {b.x - a.x, b.y - a.y}});
                std::vector<WorldPolygon> area = wedge;
                if (k > 0 && !area.empty()) area = roofparts::keepHalf(area, {n.x * (off + from), n.y * (off + from)}, n);
                if (k + 1 < rings && !area.empty()) {
                    area = roofparts::keepHalf(area, {n.x * (off + to), n.y * (off + to)}, {-n.x, -n.y});
                }
                if (!area.empty()) pieces.push_back({std::move(area), plane});
            }
        }
        roofskeleton::weldInnerPoints(pieces, input.eaves);

        // The top at a point: the face of the wedge it lies in, between the rings it lies between.
        auto top = [dome, c, planes, share, rings](Vec2 p) {
            const std::size_t sides = dome.ring.size();
            const Vec2 v{p.x - c.x, p.y - c.y};
            std::size_t best = 0;
            double bestScore = -1e18;
            for (std::size_t i = 0; i < sides; ++i) {
                const Vec2 a = dome.ring[i];
                const Vec2 b = dome.ring[(i + 1) % sides];
                const Vec2 toA{a.x - c.x, a.y - c.y};
                const Vec2 toB{b.x - c.x, b.y - c.y};
                const double left = (toA.x * v.y - toA.y * v.x) / std::hypot(toA.x, toA.y);
                const double right = (v.x * toB.y - v.y * toB.x) / std::hypot(toB.x, toB.y);
                const double score = std::min(left, right);
                if (score > bestScore) {
                    bestScore = score;
                    best = i;
                }
            }
            const double inside = dome.normals[best].x * p.x + dome.normals[best].y * p.y - dome.offsets[best];
            const double s = 1.0 - inside / dome.depths[best];
            int k = 0;
            while (k + 1 < rings && s < share[k + 1]) ++k;
            return planes[best * static_cast<std::size_t>(rings) + static_cast<std::size_t>(k)].at(p);
        };
        return roofparts::pitchedFaces(input, pieces, breaks, top);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<DomeRoof>());

}

}
