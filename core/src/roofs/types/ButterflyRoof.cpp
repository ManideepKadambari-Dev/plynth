#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

// A butterfly roof: two planes falling inward to a valley along the direction. The valley lies in the middle of
// the section's extent across, moved by ridgeOffset of that width to the left of the direction, the way a
// saltbox moves its ridge. The valley meets the outline at base, so both sides rise from it at the pitch.
class ButterflyRoof final : public RoofType {
public:
    std::string_view id() const override { return "butterfly"; }
    std::string_view name() const override { return "Butterfly"; }
    int sortKey() const override { return 66; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesRidgeOffset() const override { return true; }
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
        const double valley = std::clamp((lo + hi) / 2.0 + input.settings.ridgeOffset * (hi - lo), lo, hi);
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double base = input.base;
        const Vec2 onValley{n.x * valley, n.y * valley};

        // Left of the valley the roof rises along n, right of it against n.
        roofparts::Piece left;
        left.area = roofparts::keepHalf(input.eaves, onValley, n);
        left.top = {slope * n.x, slope * n.y, base - valley * slope};
        roofparts::Piece right;
        right.area = roofparts::keepHalf(input.eaves, onValley, {-n.x, -n.y});
        right.top = {-slope * n.x, -slope * n.y, base + valley * slope};

        std::vector<roofparts::Piece> pieces;
        if (!left.area.empty()) pieces.push_back(std::move(left));
        if (!right.area.empty()) pieces.push_back(std::move(right));
        auto height = [=](Vec2 p) { return base + std::fabs(n.x * p.x + n.y * p.y - valley) * slope; };
        return roofparts::pitchedFaces(input, pieces, {{onValley, input.direction}}, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<ButterflyRoof>());

}

}
