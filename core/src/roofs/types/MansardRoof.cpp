#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"
#include "roofs/Valleys.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace plynth {

namespace {

// The hip the slopes are measured on rises 1 m for every metre in from the section's outline, so a point's height
// on it above base is how far in the point lies.
constexpr double kMeasurePitch = 45.0;

// A mansard roof: the hip roof of the section with two slopes on every side. From the outline the lower slope
// rises at the pitch over breakRatio of the run, the farthest any point lies in from the outline, to a level
// break all round; above it the upper slope rises at the second pitch to the hips and ridges. It is worked out
// on the hip's straight skeleton, so it roofs any outline the hip roofs; other sections get a gambrel.
class MansardRoof final : public RoofType {
public:
    std::string_view id() const override { return "mansard"; }
    std::string_view name() const override { return "Mansard"; }
    int sortKey() const override { return 62; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return false; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesSecondPitch() const override { return true; }
    bool usesBreakRatio() const override { return true; }
    std::string_view fallback() const override { return "gambrel"; }

    Status fits(const RoofInput& input) const override
    {
        const RoofType* hip = findRoofType(kHipRoofId);
        if (hip && hip->fits(input).ok()) return {};
        return fail(ErrorCode::InvalidValue,
                    "The slopes of a mansard roof could not be worked out for this section, so it is built as a gambrel.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const RoofType* hip = findRoofType(kHipRoofId);
        if (!hip) return {};
        RoofInput measure = input;
        measure.settings.pitch = kMeasurePitch;
        const std::vector<RoofFace> hipFaces = hip->build(measure);
        const double base = input.base;

        double run = 0.0;
        for (const RoofFace& face : hipFaces) {
            if (face.part != RoofPart::Covering) continue;
            for (const Vec3& p : face.outline) run = std::max(run, p.z - base);
        }
        if (run <= 1e-9) return hip->build(input);

        const double lower = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double upper = std::tan(input.settings.secondPitch * std::numbers::pi / 180.0);
        const double curb = std::clamp(input.settings.breakRatio, 0.0, 1.0) * run;

        // On a measured piece the distance in from the outline is a * x + b * y + (c - base); each piece is cut where
        // that reaches curb, the part below on the lower slope and the part above on the upper one.
        std::vector<roofparts::Piece> pieces;
        for (const roofparts::Piece& piece : roofparts::coveringPieces(hipFaces)) {
            const Vec2 g{piece.top.a, piece.top.b};
            const double in = piece.top.c - base;
            const roofparts::Plane low{lower * g.x, lower * g.y, base + lower * in};
            const roofparts::Plane high{upper * g.x, upper * g.y, base + lower * curb + upper * (in - curb)};
            const double size2 = g.x * g.x + g.y * g.y;
            if (size2 < 1e-12) {
                pieces.push_back({piece.area, in <= curb ? low : high});
                continue;
            }
            const Vec2 onCurb{g.x * (curb - in) / size2, g.y * (curb - in) / size2};
            std::vector<WorldPolygon> lowerPart = roofparts::keepHalf(piece.area, onCurb, {-g.x, -g.y});
            std::vector<WorldPolygon> upperPart = roofparts::keepHalf(piece.area, onCurb, g);
            if (!lowerPart.empty()) pieces.push_back({std::move(lowerPart), low});
            if (!upperPart.empty()) pieces.push_back({std::move(upperPart), high});
        }
        return roofparts::pieceFaces(pieces, pieces, input.above);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<MansardRoof>());

}

}
