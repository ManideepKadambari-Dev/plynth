#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

// The part of the polygons whose distance along n lies from from to to. An end left out does not bound it.
std::vector<WorldPolygon> strip(const std::vector<WorldPolygon>& polygons, Vec2 n, const double* from, const double* to)
{
    std::vector<WorldPolygon> out = polygons;
    if (from && !out.empty()) out = roofparts::keepHalf(out, {n.x * *from, n.y * *from}, n);
    if (to && !out.empty()) out = roofparts::keepHalf(out, {n.x * *to, n.y * *to}, {-n.x, -n.y});
    return out;
}

// An M-shaped roof: two gables side by side along the direction, with a valley between them. Each gable takes half
// of the section's extent across, its ridge in the middle of that half; the valley meets the outline at base.
class MShapedRoof final : public RoofType {
public:
    std::string_view id() const override { return "m_shaped"; }
    std::string_view name() const override { return "M-shaped"; }
    int sortKey() const override { return 74; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    Status fits(const RoofInput&) const override { return {}; }
    std::string_view fallback() const override { return kFlatRoofId; }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const Vec2 n = roofparts::leftOf(input.direction);
        double lo = 1e18;
        double hi = -1e18;
        for (const Vec2& p : input.section.outline) {
            const double s = n.x * p.x + n.y * p.y;
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
        const double quarter = (hi - lo) / 4.0;
        const double mid = (lo + hi) / 2.0;
        const double firstRidge = lo + quarter;
        const double secondRidge = hi - quarter;
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double base = input.base;
        const Vec2 up{slope * n.x, slope * n.y};
        const Vec2 down{-slope * n.x, -slope * n.y};

        // From the right of the direction to its left: up to a ridge, down to the valley, up and down again.
        std::vector<roofparts::Piece> pieces;
        auto add = [&](const double* from, const double* to, Vec2 g, double c) {
            roofparts::Piece piece;
            piece.area = strip(input.eaves, n, from, to);
            piece.top = {g.x, g.y, c};
            if (!piece.area.empty()) pieces.push_back(std::move(piece));
        };
        add(nullptr, &firstRidge, up, base - lo * slope);
        add(&firstRidge, &mid, down, base + mid * slope);
        add(&mid, &secondRidge, up, base - mid * slope);
        add(&secondRidge, nullptr, down, base + hi * slope);

        std::vector<roofparts::BreakLine> breaks;
        for (const double s : {firstRidge, mid, secondRidge}) breaks.push_back({{n.x * s, n.y * s}, input.direction});
        auto height = [=](Vec2 p) {
            const double s = n.x * p.x + n.y * p.y;
            return base + (quarter - std::fabs(std::fabs(s - mid) - quarter)) * slope;
        };
        return roofparts::pitchedFaces(input, pieces, breaks, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<MShapedRoof>());

}

}
