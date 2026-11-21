#include "openings/ShapeSupport.h"

namespace plynth::shapes {

namespace {

// Straight sides, then two arcs of radius width that meet at the top middle.
class PointedArchShape final : public BuiltInShape {
public:
    PointedArchShape()
        : BuiltInShape(kPointedArchShapeId, "Pointed arch", "The height must be at least 0.866 times the width.", 40)
    {
    }

    bool fits(double width, double height) const override { return height >= rise(width) - 1e-9; }

    std::vector<Vec2> outline(double width, double height) const override
    {
        const double spring = height - rise(width);
        const int n = arcSegmentCount(width, kPi / 3.0);
        std::vector<Vec2> out;
        appendPoint(out, {-width / 2.0, 0.0});
        appendPoint(out, {width / 2.0, 0.0});
        // The right arc is centred on the left side's top end and runs up to the top middle.
        appendArc(out, {-width / 2.0, spring}, width, 0.0, kPi / 3.0, n);
        // The left arc is centred on the right side's top end and runs down from the top middle.
        appendArc(out, {width / 2.0, spring}, width, 2.0 * kPi / 3.0, kPi, n);
        finishOutline(out, width, height);
        return out;
    }

private:
    // How far the top middle is above the top of the straight sides.
    static double rise(double width) { return width * std::sqrt(3.0) / 2.0; }
};

const OpeningShapeRegistrar registrar(std::make_unique<PointedArchShape>());

}

}
