#include "openings/ShapeSupport.h"

namespace plynth::shapes {

namespace {

// Straight sides, then a shallow arc that rises a sixth of the width above its ends.
class FlatArchShape final : public BuiltInShape {
public:
    FlatArchShape() : BuiltInShape(kFlatArchShapeId, "Flat arch", "The height must be at least a sixth of the width.", 30) {}

    bool fits(double width, double height) const override { return height >= width / 6.0 - 1e-9; }

    std::vector<Vec2> outline(double width, double height) const override
    {
        const double rise = width / 6.0;
        // The circle through both ends of the arc and its top has radius (c^2 / 4 + b^2) / (2 b) for chord c and rise b.
        const double radius = (width * width / 4.0 + rise * rise) / (2.0 * rise);
        const Vec2 centre{0.0, height - radius};
        const double endAngle = std::atan2(radius - rise, width / 2.0);
        const double sweep = kPi - 2.0 * endAngle;
        std::vector<Vec2> out;
        appendPoint(out, {-width / 2.0, 0.0});
        appendPoint(out, {width / 2.0, 0.0});
        appendArc(out, centre, radius, endAngle, kPi - endAngle, evenCount(arcSegmentCount(radius, sweep)));
        finishOutline(out, width, height);
        return out;
    }
};

const OpeningShapeRegistrar registrar(std::make_unique<FlatArchShape>());

}

}
