#include "plynth/core/Geometry.h"
#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <utility>

namespace plynth {

namespace {

// A point of a roof's profile across the section: its distance along n and the height of the top there.
struct ProfilePoint {
    double s = 0.0;
    double z = 0.0;
};

// Faces of a roof whose top follows a profile across n, one plane strip between each two profile points. The
// first and last strips carry on over the eaves beyond the profile's ends.
std::vector<RoofFace> profileFaces(const RoofInput& input, Vec2 n, const std::vector<ProfilePoint>& profile)
{
    const std::size_t count = profile.size() - 1;
    std::vector<roofparts::Plane> planes(count);
    std::vector<roofparts::Piece> pieces;
    std::vector<roofparts::BreakLine> breaks;
    const Vec2 along{-n.y, n.x};
    for (std::size_t k = 0; k < count; ++k) {
        const ProfilePoint a = profile[k];
        const ProfilePoint b = profile[k + 1];
        const double m = (b.z - a.z) / (b.s - a.s);
        planes[k] = {m * n.x, m * n.y, a.z - m * a.s};
        std::vector<WorldPolygon> area = input.eaves;
        if (k > 0 && !area.empty()) area = roofparts::keepHalf(area, {n.x * a.s, n.y * a.s}, n);
        if (k + 1 < count && !area.empty()) area = roofparts::keepHalf(area, {n.x * b.s, n.y * b.s}, {-n.x, -n.y});
        if (k > 0) breaks.push_back({{n.x * a.s, n.y * a.s}, along});
        if (!area.empty()) pieces.push_back({std::move(area), planes[k]});
    }
    auto height = [planes, profile, n](Vec2 p) {
        const double s = n.x * p.x + n.y * p.y;
        std::size_t k = 0;
        while (k + 1 < planes.size() && s > profile[k + 1].s) ++k;
        return planes[k].at(p);
    };
    return roofparts::pitchedFaces(input, pieces, breaks, height);
}

// The highest rise a curved roof over a run can have before it meets the walls steeper than kMaxRoofPitch, when
// the curve stands upright at the run's end.
double highestRise(double run)
{
    return run * std::tan(kMaxRoofPitch / 2.0 * std::numbers::pi / 180.0);
}

// A barrel roof: a vault along the direction whose top is an arc across the section, standing on the outline at
// base on both sides and rise higher in the middle. The arc is cut into flat strips by arcSegmentCount, and the
// outer strips carry on over the eaves. A rise that would meet the walls steeper than kMaxRoofPitch is refused.
class BarrelRoof final : public RoofType {
public:
    std::string_view id() const override { return "barrel"; }
    std::string_view name() const override { return "Barrel"; }
    int sortKey() const override { return 80; }
    bool usesPitch() const override { return false; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesRise() const override { return true; }
    std::string_view fallback() const override { return kFlatRoofId; }

    Status fits(const RoofInput& input) const override
    {
        const auto [lo, hi] = extentAcross(input);
        const double highest = highestRise((hi - lo) / 2.0);
        if (input.settings.rise <= highest + 1e-9) return {};
        char text[32];
        std::snprintf(text, sizeof text, "%.2f", highest);
        return fail(ErrorCode::InvalidValue, "A barrel roof on this section can rise at most " + std::string(text) +
                                                 " m before it meets the walls too steeply, so it is built flat.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const Vec2 n = roofparts::leftOf(input.direction);
        const auto [lo, hi] = extentAcross(input);
        const double half = (hi - lo) / 2.0;
        const double mid = (lo + hi) / 2.0;
        const double rise = input.settings.rise;
        if (!(half > 1e-6) || !(rise > 0.0)) return {};
        const double radius = (half * half + rise * rise) / (2.0 * rise);
        const double sweep = std::asin(std::min(1.0, half / radius));
        const int segments = arcSegmentCount(radius, 2.0 * sweep);

        std::vector<ProfilePoint> profile;
        for (int k = 0; k <= segments; ++k) {
            const double angle = -sweep + 2.0 * sweep * k / segments;
            profile.push_back({mid + radius * std::sin(angle), input.base + radius * (std::cos(angle) - std::cos(sweep))});
        }
        profile.front() = {lo, input.base};
        profile.back() = {hi, input.base};
        return profileFaces(input, n, profile);
    }

private:
    // The section's extent across the direction, as distances along the left of it.
    static std::pair<double, double> extentAcross(const RoofInput& input)
    {
        const Vec2 n = roofparts::leftOf(input.direction);
        double lo = 1e18;
        double hi = -1e18;
        for (const Vec2& p : input.section.outline) {
            const double s = n.x * p.x + n.y * p.y;
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
        return {lo, hi};
    }
};

const RoofTypeRegistrar registrar(std::make_unique<BarrelRoof>());

}

}
