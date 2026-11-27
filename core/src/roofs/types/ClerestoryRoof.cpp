#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"
#include "roofs/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

constexpr double kOnStep = 1e-6;

// Says whether a face lies on the vertical line where the two sheds meet.
bool onStep(const RoofFace& face, Vec2 n, double step)
{
    for (const Vec3& p : face.outline) {
        if (std::fabs(n.x * p.x + n.y * p.y - step) > kOnStep) return false;
    }
    return true;
}

// A clerestory roof: two sheds meeting along the direction at different heights, with a vertical band of glass
// between them. The step lies in the middle of the section's extent across, moved by ridgeOffset of that width to
// the left of the direction, the way a saltbox moves its ridge. Right of the step the low shed rises from the
// outline to the step; left of it the high shed starts rise above the low shed's top (the glass) plus its own
// thickness and falls away to the outline on the left. The glass is taller where the high shed would otherwise
// reach the outline below base. No wall stands under the step, so the rooms below stay open up to the glass.
// Sections with holes or inward corners are refused, since the step would run along or out of their walls.
class ClerestoryRoof final : public RoofType {
public:
    std::string_view id() const override { return "clerestory"; }
    std::string_view name() const override { return "Clerestory"; }
    int sortKey() const override { return 70; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesRidgeOffset() const override { return true; }
    bool usesRise() const override { return true; }
    std::string_view fallback() const override { return kShedRoofId; }

    Status fits(const RoofInput& input) const override
    {
        const std::vector<Vec2> ring = roofskeleton::withoutStraightPoints(input.section.outline);
        if (input.section.holes.empty() && ring.size() >= 3 && roofskeleton::convexRing(ring)) return {};
        return fail(ErrorCode::InvalidValue,
                    "A clerestory roof needs a section without holes or inward corners, so it is built as a shed.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const Vec2 n = roofparts::leftOf(input.direction);
        const Vec2 back{-n.x, -n.y};
        double lo = 1e18;
        double hi = -1e18;
        for (const Vec2& p : input.section.outline) {
            const double s = n.x * p.x + n.y * p.y;
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
        const double step = std::clamp((lo + hi) / 2.0 + input.settings.ridgeOffset * (hi - lo), lo, hi);
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double base = input.base;
        const double low = base + (step - lo) * slope;
        const double high = std::max(low + input.settings.rise + kRoofThickness, base + (hi - step) * slope);
        const double glass = high - kRoofThickness - low;
        const Vec2 onStepLine{n.x * step, n.y * step};

        std::vector<RoofFace> faces;
        auto add = [&faces, n, step](std::vector<RoofFace> more) {
            for (RoofFace& face : more) {
                if (face.part == RoofPart::Gable && onStep(face, n, step)) continue;
                faces.push_back(std::move(face));
            }
        };

        // Each shed is built as a roof of its own over its half, then the gable fills under the step are dropped.
        auto shed = [&](Vec2 side, const roofparts::Plane& top) {
            RoofInput half = input;
            half.eaves = roofparts::keepHalf(input.eaves, onStepLine, side);
            const std::vector<WorldPolygon> sections = roofparts::keepHalf({input.section}, onStepLine, side);
            std::vector<roofparts::Piece> pieces;
            if (!half.eaves.empty()) pieces.push_back({half.eaves, top});
            half.section = sections.empty() ? WorldPolygon{} : sections.front();
            auto height = [top](Vec2 p) { return top.at(p); };
            add(roofparts::pitchedFaces(half, pieces, {}, height));
            for (std::size_t k = 1; k < sections.size(); ++k) {
                RoofInput more = input;
                more.eaves.clear();
                more.section = sections[k];
                add(roofparts::pitchedFaces(more, {}, {}, height));
            }
            return half.eaves;
        };
        const std::vector<WorldPolygon> lowEaves =
            shed(back, {slope * n.x, slope * n.y, base - lo * slope});
        shed(n, {-slope * n.x, -slope * n.y, high + step * slope});

        // The glass stands on the low shed along the step, looking out over it.
        for (const WorldPolygon& poly : lowEaves) {
            std::vector<const std::vector<Vec2>*> rings{&poly.outline};
            for (const auto& hole : poly.holes) rings.push_back(&hole);
            for (const std::vector<Vec2>* ring : rings) {
                for (std::size_t i = 0; i < ring->size(); ++i) {
                    const Vec2 p = (*ring)[i];
                    const Vec2 q = (*ring)[(i + 1) % ring->size()];
                    if (std::fabs(n.x * p.x + n.y * p.y - step) > kOnStep ||
                        std::fabs(n.x * q.x + n.y * q.y - step) > kOnStep) {
                        continue;
                    }
                    for (const auto& [u, v] : roofparts::freeParts(q, p, input.above)) {
                        if (std::hypot(v.x - u.x, v.y - u.y) < 1e-6) continue;
                        faces.push_back(roofparts::wallFace(u, v, low, low, low + glass, low + glass, RoofPart::Glazing));
                    }
                }
            }
        }
        return faces;
    }
};

const RoofTypeRegistrar registrar(std::make_unique<ClerestoryRoof>());

}

}
