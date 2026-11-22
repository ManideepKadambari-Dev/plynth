#include "plynth/core/Stairs.h"
#include "stairs/StairSupport.h"

namespace plynth::stairs {

namespace {

// One flight of treads straight ahead.
class StraightStair final : public BuiltInStair {
public:
    StraightStair() : BuiltInStair(kStraightStairId, "Straight", 10, false) {}

protected:
    StairShape rightShape(const StairInput& input) const override
    {
        const int treads = input.riserCount - 1;
        const double half = input.width / 2.0;
        StairShape shape;
        for (int i = 0; i < treads; ++i) {
            shape.steps.push_back(box(StairPart::Step, -half, half, i * kStairGoing, (i + 1) * kStairGoing, 0.0,
                                      (i + 1) * input.riser));
        }
        shape.walkLine = {{0.0, 0.0}, {0.0, treads * kStairGoing}};
        return shape;
    }
};

const StairTypeRegistrar registrar(std::make_unique<StraightStair>());

}

}
