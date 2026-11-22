#include "plynth/core/Stairs.h"
#include "stairs/StairSupport.h"

namespace plynth::stairs {

namespace {

// Wedge steps winding round a centre column.
class SpiralStair final : public BuiltInStair {
public:
    SpiralStair() : BuiltInStair(kSpiralStairId, "Spiral", 40, true) {}

protected:
    StairShape rightShape(const StairInput& input) const override
    {
        const int treads = input.riserCount - 1;
        const double walkRadius = kSpiralColumnRadius + input.width / 2.0;
        const double outer = kSpiralColumnRadius + input.width;
        const Vec2 centre{walkRadius, 0.0};
        const double turn = kStairGoing / walkRadius;
        const int outerCuts = arcSegmentCount(outer, turn);

        // The point at this radius and angle round the column. Angle 0 puts the walk line at (0, 0).
        const auto at = [&](double radius, double angle) {
            return Vec2{centre.x - radius * std::cos(angle), centre.y + radius * std::sin(angle)};
        };

        StairShape shape;
        for (int i = 0; i < treads; ++i) {
            const double a0 = i * turn;
            const double a1 = (i + 1) * turn;
            StairStep step;
            step.part = StairPart::Step;
            step.outline.push_back(at(kSpiralColumnRadius, a0));
            for (int k = 0; k <= outerCuts; ++k) {
                step.outline.push_back(at(outer, a0 + (a1 - a0) * k / outerCuts));
            }
            step.outline.push_back(at(kSpiralColumnRadius, a1));
            orient(step.outline);
            step.top = (i + 1) * input.riser;
            step.bottom = step.top - kSpiralTreadThickness;
            shape.steps.push_back(std::move(step));
            if (i == 0) shape.walkLine.push_back(at(walkRadius, a0));
            shape.walkLine.push_back(at(walkRadius, a1));
        }

        StairStep column;
        column.part = StairPart::Column;
        const int cuts = std::max(8, arcSegmentCount(kSpiralColumnRadius, 2.0 * std::numbers::pi));
        for (int k = 0; k < cuts; ++k) {
            column.outline.push_back(at(kSpiralColumnRadius, 2.0 * std::numbers::pi * k / cuts));
        }
        orient(column.outline);
        column.bottom = 0.0;
        column.top = input.rise;
        shape.steps.push_back(std::move(column));
        return shape;
    }
};

const StairTypeRegistrar registrar(std::make_unique<SpiralStair>());

}

}
