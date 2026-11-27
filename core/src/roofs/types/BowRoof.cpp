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

// The way the roof falls: the set direction, or across the ridge the section's shape suggests, as a shed falls.
Vec2 fallOf(const RoofInput& input)
{
    if (input.settings.direction) return input.direction;
    return std::fabs(input.direction.x) >= std::fabs(input.direction.y) ? Vec2{0.0, -1.0} : Vec2{1.0, 0.0};
}

// The section's extent against the way it falls: lo at the low side, hi at the high side.
std::pair<double, double> extentAgainst(const RoofInput& input)
{
    const Vec2 fall = fallOf(input);
    double lo = 1e18;
    double hi = -1e18;
    for (const Vec2& p : input.section.outline) {
        const double s = -(fall.x * p.x + fall.y * p.y);
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    return {lo, hi};
}

// A bow roof: a curved shed. The top is an arc that stands on the outline at base on the low side, the side the
// roof falls toward, and rises to rise higher on the high side, where it runs level. The arc is cut into flat
// strips by arcSegmentCount, and the outer strips carry on over the eaves. A rise that would meet the low side
// steeper than kMaxRoofPitch is refused.
class BowRoof final : public RoofType {
public:
    std::string_view id() const override { return "bow"; }
    std::string_view name() const override { return "Bow"; }
    int sortKey() const override { return 84; }
    bool usesPitch() const override { return false; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesRise() const override { return true; }
    std::string_view fallback() const override { return kFlatRoofId; }

    Status fits(const RoofInput& input) const override
    {
        const auto [lo, hi] = extentAgainst(input);
        const double highest = (hi - lo) * std::tan(kMaxRoofPitch / 2.0 * std::numbers::pi / 180.0);
        if (input.settings.rise <= highest + 1e-9) return {};
        char text[32];
        std::snprintf(text, sizeof text, "%.2f", highest);
        return fail(ErrorCode::InvalidValue, "A bow roof on this section can rise at most " + std::string(text) +
                                                 " m before it meets the walls too steeply, so it is built flat.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const Vec2 fall = fallOf(input);
        const Vec2 n{-fall.x, -fall.y};
        const auto [lo, hi] = extentAgainst(input);
        const double run = hi - lo;
        const double rise = input.settings.rise;
        if (!(run > 1e-6) || !(rise > 0.0)) return {};
        const double radius = (run * run + rise * rise) / (2.0 * rise);
        const double sweep = std::asin(std::min(1.0, run / radius));
        const int segments = arcSegmentCount(radius, sweep);

        // From the low side, where the arc is steepest, to the high side, where it runs level.
        std::vector<ProfilePoint> profile;
        for (int k = 0; k <= segments; ++k) {
            const double angle = sweep * (1.0 - static_cast<double>(k) / segments);
            profile.push_back({hi - radius * std::sin(angle), input.base + rise - radius + radius * std::cos(angle)});
        }
        profile.front() = {lo, input.base};
        profile.back() = {hi, input.base + rise};
        return profileFaces(input, n, profile);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<BowRoof>());

}

}
