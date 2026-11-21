#include "openings/ShapeSupport.h"

namespace plynth::shapes {

namespace {

// The whole box.
class RectangleShape final : public BuiltInShape {
public:
    RectangleShape() : BuiltInShape(kRectangleShapeId, "Rectangle", "", 10) {}

    bool fits(double, double) const override { return true; }

    std::vector<Vec2> outline(double width, double height) const override
    {
        const double h = width / 2.0;
        return {{-h, 0.0}, {h, 0.0}, {h, height}, {-h, height}};
    }
};

const OpeningShapeRegistrar registrar(std::make_unique<RectangleShape>());

}

}
