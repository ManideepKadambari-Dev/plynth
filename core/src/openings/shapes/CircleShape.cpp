#include "openings/ShapeSupport.h"

namespace plynth::shapes {

namespace {

// The ellipse that touches all four sides of the box, a circle when the box is square.
class CircleShape final : public BuiltInShape {
public:
    CircleShape() : BuiltInShape(kCircleShapeId, "Circle", "", 50) {}

    bool fits(double, double) const override { return true; }

    std::vector<Vec2> outline(double width, double height) const override
    {
        const double a = width / 2.0;
        const double b = height / 2.0;
        // A multiple of four, so the points on the four sides are corners of the outline.
        int n = arcSegmentCount(std::max(a, b), 2.0 * kPi);
        n = (n + 3) / 4 * 4;
        static constexpr double exactCos[4] = {0.0, 1.0, 0.0, -1.0};
        static constexpr double exactSin[4] = {-1.0, 0.0, 1.0, 0.0};
        std::vector<Vec2> out;
        out.reserve(static_cast<std::size_t>(n));
        // Starts at the bottom and runs anticlockwise.
        for (int k = 0; k < n; ++k) {
            double c = 0.0;
            double s = 0.0;
            if ((k * 4) % n == 0) {
                c = exactCos[k * 4 / n];
                s = exactSin[k * 4 / n];
            } else {
                const double t = -kPi / 2.0 + 2.0 * kPi * k / n;
                c = std::cos(t);
                s = std::sin(t);
            }
            out.push_back({a * c, b + b * s});
        }
        finishOutline(out, width, height);
        return out;
    }
};

const OpeningShapeRegistrar registrar(std::make_unique<CircleShape>());

}

}
