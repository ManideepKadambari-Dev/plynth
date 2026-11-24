#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

// A gable roof: two planes falling away from a ridge along the direction through the middle of the section.
class GableRoof final : public RoofType {
public:
    std::string_view id() const override { return kGableRoofId; }
    std::string_view name() const override { return "Gable"; }
    int sortKey() const override { return 20; }
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
        const double mid = (lo + hi) / 2.0;
        const double half = (hi - lo) / 2.0;
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const Vec2 onRidge{n.x * mid, n.y * mid};

        roofparts::Piece sideA;
        sideA.area = roofparts::keepHalf(input.eaves, onRidge, n);
        sideA.top = {-slope * n.x, -slope * n.y, input.base + (half + mid) * slope};
        roofparts::Piece sideB;
        sideB.area = roofparts::keepHalf(input.eaves, onRidge, {-n.x, -n.y});
        sideB.top = {slope * n.x, slope * n.y, input.base + (half - mid) * slope};

        const double base = input.base;
        auto height = [=](Vec2 p) { return base + (half - std::fabs(n.x * p.x + n.y * p.y - mid)) * slope; };
        return roofparts::pitchedFaces(input, {sideA, sideB}, {{onRidge, input.direction}}, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<GableRoof>());

}

}
