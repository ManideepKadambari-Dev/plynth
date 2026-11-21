#include "openings/ShapeSupport.h"

namespace plynth::shapes {

namespace {

// Straight sides, then a half circle on top.
class RoundArchShape final : public BuiltInShape {
public:
    RoundArchShape() : BuiltInShape(kRoundArchShapeId, "Round arch", "The height must be at least half the width.", 20) {}

    bool fits(double width, double height) const override { return height >= width / 2.0 - 1e-9; }

    std::vector<Vec2> outline(double width, double height) const override
    {
        const double r = width / 2.0;
        std::vector<Vec2> out;
        appendPoint(out, {-r, 0.0});
        appendPoint(out, {r, 0.0});
        appendArc(out, {0.0, height - r}, r, 0.0, kPi, evenCount(arcSegmentCount(r, kPi)));
        finishOutline(out, width, height);
        return out;
    }
};

const OpeningShapeRegistrar registrar(std::make_unique<RoundArchShape>());

}

}
