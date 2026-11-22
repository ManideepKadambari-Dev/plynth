#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

// A shed roof: one plane falling toward one side, lowest at the section's outline on that side.
class ShedRoof final : public RoofType {
public:
    std::string_view id() const override { return kShedRoofId; }
    std::string_view name() const override { return "Shed"; }
    int sortKey() const override { return 40; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    Status fits(const RoofInput&) const override { return {}; }
    std::string_view fallback() const override { return kFlatRoofId; }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        // Without a set direction, input.direction is the ridge the section's shape suggests, so the roof
        // falls across it: toward +y on the plan when the ridge runs along x, toward +x otherwise.
        Vec2 fall = input.direction;
        if (!input.settings.direction) {
            fall = std::fabs(input.direction.x) >= std::fabs(input.direction.y) ? Vec2{0.0, -1.0} : Vec2{1.0, 0.0};
        }
        double highest = -1e18;
        for (const Vec2& p : input.section.outline) highest = std::max(highest, fall.x * p.x + fall.y * p.y);
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double base = input.base;

        roofparts::Piece piece;
        piece.area = input.eaves;
        piece.top = {-slope * fall.x, -slope * fall.y, base + highest * slope};
        auto height = [=](Vec2 p) { return base + (highest - (fall.x * p.x + fall.y * p.y)) * slope; };
        return roofparts::pitchedFaces(input, {piece}, {}, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<ShedRoof>());

}

}
