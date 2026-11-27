#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

// The plane that stands height high at a point and rises along g.
roofparts::Plane planeThrough(Vec2 g, Vec2 at, double height)
{
    return {g.x, g.y, height - g.x * at.x - g.y * at.y};
}

// A jerkinhead (clipped gable) roof: a gable along the direction whose two ends are clipped by a small hip. The
// gable end walls rise to breakRatio of the ridge's height above base, and from there a plane at the same pitch
// rises inward from each end of the section to the ridge.
class JerkinheadRoof final : public RoofType {
public:
    std::string_view id() const override { return "jerkinhead"; }
    std::string_view name() const override { return "Jerkinhead"; }
    int sortKey() const override { return 36; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesBreakRatio() const override { return true; }
    Status fits(const RoofInput&) const override { return {}; }
    std::string_view fallback() const override { return kFlatRoofId; }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const Vec2 d = input.direction;
        const Vec2 n = roofparts::leftOf(d);
        double lo = 1e18;
        double hi = -1e18;
        double first = 1e18;
        double last = -1e18;
        for (const Vec2& p : input.section.outline) {
            const double across = n.x * p.x + n.y * p.y;
            const double along = d.x * p.x + d.y * p.y;
            lo = std::min(lo, across);
            hi = std::max(hi, across);
            first = std::min(first, along);
            last = std::max(last, along);
        }
        const double mid = (lo + hi) / 2.0;
        const double half = (hi - lo) / 2.0;
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double ridge = input.base + half * slope;
        const double clip = input.base + std::clamp(input.settings.breakRatio, 0.0, 1.0) * half * slope;
        const Vec2 onRidge{n.x * mid, n.y * mid};

        const std::vector<roofparts::Plane> planes{
            planeThrough({-slope * n.x, -slope * n.y}, onRidge, ridge),
            planeThrough({slope * n.x, slope * n.y}, onRidge, ridge),
            planeThrough({slope * d.x, slope * d.y}, {d.x * first, d.y * first}, clip),
            planeThrough({-slope * d.x, -slope * d.y}, {d.x * last, d.y * last}, clip),
        };

        // Each piece is where its plane is the lowest; the breaks are every line where two planes cross.
        std::vector<roofparts::Piece> pieces;
        std::vector<roofparts::BreakLine> breaks;
        for (std::size_t i = 0; i < planes.size(); ++i) {
            std::vector<WorldPolygon> area = input.eaves;
            for (std::size_t j = 0; j < planes.size(); ++j) {
                if (j == i) continue;
                const Vec2 m{planes[j].a - planes[i].a, planes[j].b - planes[i].b};
                const double c = planes[j].c - planes[i].c;
                const double size2 = m.x * m.x + m.y * m.y;
                if (size2 < 1e-12) continue;
                const Vec2 point{-c * m.x / size2, -c * m.y / size2};
                if (!area.empty()) area = roofparts::keepHalf(area, point, m);
                if (j > i) breaks.push_back({point, {m.y, -m.x}});
            }
            if (area.empty()) continue;
            pieces.push_back({std::move(area), planes[i]});
        }

        auto height = [planes](Vec2 p) {
            double lowest = 1e18;
            for (const roofparts::Plane& plane : planes) lowest = std::min(lowest, plane.at(p));
            return lowest;
        };
        return roofparts::pitchedFaces(input, pieces, breaks, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<JerkinheadRoof>());

}

}
