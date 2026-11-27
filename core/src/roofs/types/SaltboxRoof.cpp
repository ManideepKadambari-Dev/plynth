#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace plynth {

namespace {

// A saltbox roof: a gable along the direction whose ridge is moved across the section by ridgeOffset of its
// width, toward the left of the direction on the plan when above 0. Both sides keep the pitch, so the long side
// reaches down to base at its eaves while the short side's eaves stand higher, its wall closed by a gable fill.
// It roofs any outline as a gable does; with an offset of 0 it is a gable.
class SaltboxRoof final : public RoofType {
public:
    std::string_view id() const override { return "saltbox"; }
    std::string_view name() const override { return "Saltbox"; }
    int sortKey() const override { return 64; }
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
        const double ridgeAt = std::clamp((lo + hi) / 2.0 + input.settings.ridgeOffset * (hi - lo), lo, hi);
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double ridge = input.base + slope * std::max(ridgeAt - lo, hi - ridgeAt);
        const Vec2 onRidge{n.x * ridgeAt, n.y * ridgeAt};

        roofparts::Piece sideA;
        sideA.area = roofparts::keepHalf(input.eaves, onRidge, n);
        sideA.top = {-slope * n.x, -slope * n.y, ridge + slope * ridgeAt};
        roofparts::Piece sideB;
        sideB.area = roofparts::keepHalf(input.eaves, onRidge, {-n.x, -n.y});
        sideB.top = {slope * n.x, slope * n.y, ridge - slope * ridgeAt};
        std::vector<roofparts::Piece> pieces;
        if (!sideA.area.empty()) pieces.push_back(std::move(sideA));
        if (!sideB.area.empty()) pieces.push_back(std::move(sideB));

        auto height = [=](Vec2 p) { return ridge - slope * std::fabs(n.x * p.x + n.y * p.y - ridgeAt); };
        return roofparts::pitchedFaces(input, pieces, {{onRidge, input.direction}}, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<SaltboxRoof>());

}

}
