#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"
#include "roofs/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

constexpr double kAFrameMinPitch = 45.0; // degrees
constexpr double kAFrameFoot = 0.3;      // metres above the first floor's level where the slopes end
constexpr double kAFrameMaxReach = 5.0;  // metres past the walls the slopes may reach

// The convex hull of points, anticlockwise.
std::vector<Vec2> hullOf(std::vector<Vec2> points)
{
    std::sort(points.begin(), points.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (points.size() < 3) return points;
    auto turn = [](Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<Vec2> hull(2 * points.size());
    std::size_t k = 0;
    for (const Vec2& p : points) {
        while (k >= 2 && turn(hull[k - 2], hull[k - 1], p) <= 0.0) --k;
        hull[k++] = p;
    }
    for (std::size_t i = points.size() - 1, low = k + 1; i-- > 0;) {
        while (k >= low && turn(hull[k - 2], hull[k - 1], points[i]) <= 0.0) --k;
        hull[k++] = points[i];
    }
    hull.resize(k - 1);
    return hull;
}

// How far past the section's outline the slopes reach across the ridge to come down to kAFrameFoot.
double reachOf(const RoofInput& input)
{
    const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
    return std::max(0.0, (input.base - kAFrameFoot) / slope);
}

// An A-frame roof: a steep gable along the direction whose two slopes carry on past the walls until they come down
// to kAFrameFoot above the first floor's level, so the walls stand under the slope. Across the ridge the eaves so
// reach further than the overhang; at the gable ends they reach the overhang. The rest is as a gable. A pitch
// under kAFrameMinPitch, a section with holes or inward corners, or slopes reaching more than kAFrameMaxReach past
// the walls are refused.
class AFrameRoof final : public RoofType {
public:
    std::string_view id() const override { return "a_frame"; }
    std::string_view name() const override { return "A-frame"; }
    int sortKey() const override { return 72; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    std::string_view fallback() const override { return kGableRoofId; }

    Status fits(const RoofInput& input) const override
    {
        if (input.settings.pitch < kAFrameMinPitch - 1e-9) {
            return fail(ErrorCode::InvalidValue,
                        "An A-frame roof needs a pitch of at least 45 degrees, so it is built as a gable.");
        }
        const std::vector<Vec2> ring = roofskeleton::withoutStraightPoints(input.section.outline);
        if (!input.section.holes.empty() || ring.size() < 3 || !roofskeleton::convexRing(ring)) {
            return fail(ErrorCode::InvalidValue,
                        "An A-frame roof needs a section without holes or inward corners, so it is built as a gable.");
        }
        if (reachOf(input) > kAFrameMaxReach) {
            return fail(ErrorCode::InvalidValue, "An A-frame roof on this floor would reach more than 5 m past the "
                                                 "walls to come down near the ground, so it is built as a gable.");
        }
        return {};
    }

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

        // The section stretched across the ridge by the reach less the overhang, then grown by the overhang as
        // the eaves are, makes eaves that reach the overhang at the ends and the reach across.
        RoofInput grown = input;
        const double overhang = input.settings.overhang;
        const double stretch = reachOf(input) - overhang;
        if (stretch > 1e-9) {
            std::vector<Vec2> points;
            for (const Vec2& p : input.section.outline) {
                points.push_back({p.x + n.x * stretch, p.y + n.y * stretch});
                points.push_back({p.x - n.x * stretch, p.y - n.y * stretch});
            }
            WorldPolygon core;
            core.outline = hullOf(std::move(points));
            std::vector<WorldPolygon> eaves{core};
            if (overhang > 1e-9) eaves = roofparts::inset(eaves, -overhang);
            if (!input.above.empty()) eaves = roofparts::without(eaves, input.above);
            grown.eaves = roofparts::tidied(eaves);
        }

        roofparts::Piece sideA;
        sideA.area = roofparts::keepHalf(grown.eaves, onRidge, n);
        sideA.top = {-slope * n.x, -slope * n.y, input.base + (half + mid) * slope};
        roofparts::Piece sideB;
        sideB.area = roofparts::keepHalf(grown.eaves, onRidge, {-n.x, -n.y});
        sideB.top = {slope * n.x, slope * n.y, input.base + (half - mid) * slope};
        std::vector<roofparts::Piece> pieces;
        if (!sideA.area.empty()) pieces.push_back(std::move(sideA));
        if (!sideB.area.empty()) pieces.push_back(std::move(sideB));

        const double base = input.base;
        auto height = [=](Vec2 p) { return base + (half - std::fabs(n.x * p.x + n.y * p.y - mid)) * slope; };
        return roofparts::pitchedFaces(grown, pieces, {{onRidge, input.direction}}, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<AFrameRoof>());

}

}
