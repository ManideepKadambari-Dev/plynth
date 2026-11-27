#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"
#include "roofs/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

constexpr std::string_view kPyramidRoofId = "pyramid";

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

// A pyramid roof: every edge of a convex section rises to one apex above the section's middle. The apex stands
// as high as the pitch reaches from the nearest edge, so no face is steeper than the pitch; on a square or a
// regular polygon every face has the pitch and it is the same as a hip.
class PyramidRoof final : public RoofType {
public:
    std::string_view id() const override { return kPyramidRoofId; }
    std::string_view name() const override { return "Pyramid"; }
    int sortKey() const override { return 32; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return false; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    std::string_view fallback() const override { return kHipRoofId; }

    Status fits(const RoofInput& input) const override
    {
        if (input.section.holes.empty() &&
            roofskeleton::convexRing(roofskeleton::withoutStraightPoints(input.section.outline))) {
            return {};
        }
        return fail(ErrorCode::InvalidValue,
                    "A pyramid roof needs a section without holes or inward corners, so it is built as a hip.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const std::vector<Vec2> ring = roofskeleton::withoutStraightPoints(input.section.outline);
        const std::size_t count = ring.size();
        if (count < 3) return {};
        const Vec2 apex = middleOf(ring);
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double base = input.base;

        // Edge i has the inward unit normal normals[i]; the distance inside it is n . p - offsets[i].
        std::vector<Vec2> normals(count);
        std::vector<double> offsets(count);
        double nearest = 1e18;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec2 a = ring[i];
            const Vec2 b = ring[(i + 1) % count];
            const double len = std::hypot(b.x - a.x, b.y - a.y);
            normals[i] = roofparts::leftOf({(b.x - a.x) / len, (b.y - a.y) / len});
            offsets[i] = normals[i].x * a.x + normals[i].y * a.y;
            nearest = std::min(nearest, normals[i].x * apex.x + normals[i].y * apex.y - offsets[i]);
        }
        if (!(nearest > 1e-9)) return {};
        const double rise = slope * nearest;

        // Each face rises from its edge at base to the apex at base + rise; it covers the wedge between the lines
        // from the apex through the edge's two ends.
        std::vector<double> rates(count);
        std::vector<roofparts::Piece> pieces;
        std::vector<roofparts::BreakLine> breaks;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec2 a = ring[i];
            const Vec2 b = ring[(i + 1) % count];
            const Vec2 toA{a.x - apex.x, a.y - apex.y};
            const Vec2 toB{b.x - apex.x, b.y - apex.y};
            rates[i] = rise / (normals[i].x * apex.x + normals[i].y * apex.y - offsets[i]);
            breaks.push_back({apex, toA});
            std::vector<WorldPolygon> area = roofparts::keepHalf(input.eaves, apex, roofparts::leftOf(toA));
            if (!area.empty()) area = roofparts::keepHalf(area, apex, {toB.y, -toB.x});
            if (area.empty()) continue;
            roofparts::Piece piece;
            piece.area = std::move(area);
            piece.top = {rates[i] * normals[i].x, rates[i] * normals[i].y, base - rates[i] * offsets[i]};
            pieces.push_back(std::move(piece));
        }

        roofskeleton::weldInnerPoints(pieces, input.eaves);

        auto height = [=](Vec2 p) {
            double lowest = 1e18;
            for (std::size_t i = 0; i < count; ++i) {
                lowest = std::min(lowest, rates[i] * (normals[i].x * p.x + normals[i].y * p.y - offsets[i]));
            }
            return base + lowest;
        };
        return roofparts::pitchedFaces(input, pieces, breaks, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<PyramidRoof>());

}

}
