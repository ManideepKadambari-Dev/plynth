#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace plynth {

namespace {

// The point at a distance along a unit direction from the origin, so the line through it across the direction
// is where that distance is reached.
Vec2 acrossAt(Vec2 n, double s)
{
    return {n.x * s, n.y * s};
}

// A gambrel (barn) roof: a gable along the direction with two slopes on each side. From each side of the
// section the lower slope rises at the pitch over breakRatio of the run (half the section's width across the
// direction), then the upper slope rises at the second pitch to the ridge in the middle. Gable fills close the
// ends as for a gable, so it roofs any outline.
class GambrelRoof final : public RoofType {
public:
    std::string_view id() const override { return "gambrel"; }
    std::string_view name() const override { return "Gambrel"; }
    int sortKey() const override { return 60; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesSecondPitch() const override { return true; }
    bool usesBreakRatio() const override { return true; }
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
        const double steep = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double shallow = std::tan(input.settings.secondPitch * std::numbers::pi / 180.0);
        const double run = std::clamp(input.settings.breakRatio, 0.0, 1.0) * half;
        const double base = input.base;
        const double curb = base + steep * run;

        // Side A lies where n . p is above the middle, side B below it; each has a lower and an upper plane.
        const roofparts::Plane lowerA{-steep * n.x, -steep * n.y, base + steep * hi};
        const roofparts::Plane upperA{-shallow * n.x, -shallow * n.y, curb + shallow * (hi - run)};
        const roofparts::Plane upperB{shallow * n.x, shallow * n.y, curb - shallow * (lo + run)};
        const roofparts::Plane lowerB{steep * n.x, steep * n.y, base - steep * lo};

        std::vector<roofparts::Piece> pieces;
        const auto add = [&pieces](std::vector<WorldPolygon> area, const roofparts::Plane& plane) {
            if (!area.empty()) pieces.push_back({std::move(area), plane});
        };
        const Vec2 back{-n.x, -n.y};
        add(roofparts::keepHalf(input.eaves, acrossAt(n, hi - run), n), lowerA);
        std::vector<WorldPolygon> sideA = roofparts::keepHalf(input.eaves, acrossAt(n, mid), n);
        if (!sideA.empty()) add(roofparts::keepHalf(sideA, acrossAt(n, hi - run), back), upperA);
        std::vector<WorldPolygon> sideB = roofparts::keepHalf(input.eaves, acrossAt(n, mid), back);
        if (!sideB.empty()) add(roofparts::keepHalf(sideB, acrossAt(n, lo + run), n), upperB);
        add(roofparts::keepHalf(input.eaves, acrossAt(n, lo + run), back), lowerB);

        const std::vector<roofparts::BreakLine> breaks{{acrossAt(n, mid), input.direction},
                                                       {acrossAt(n, hi - run), input.direction},
                                                       {acrossAt(n, lo + run), input.direction}};
        auto height = [=](Vec2 p) {
            const double t = half - std::fabs(n.x * p.x + n.y * p.y - mid);
            return t <= run ? base + steep * t : curb + shallow * (t - run);
        };
        return roofparts::pitchedFaces(input, pieces, breaks, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<GambrelRoof>());

}

}
