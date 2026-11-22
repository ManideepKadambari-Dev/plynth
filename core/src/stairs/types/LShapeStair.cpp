#include "plynth/core/Stairs.h"
#include "stairs/StairSupport.h"

namespace plynth::stairs {

namespace {

// A flight, a square landing, then a flight turned a quarter to the right.
class LShapeStair final : public BuiltInStair {
public:
    LShapeStair() : BuiltInStair(kLShapeStairId, "L shape", 20, true) {}

protected:
    StairShape rightShape(const StairInput& input) const override
    {
        const int treads = input.riserCount - 1;
        const int first = treads / 2;
        const int second = treads - first - 1;
        const double w = input.width;
        const double half = w / 2.0;
        const double g = kStairGoing;
        const double landingV = first * g;

        StairShape shape;
        for (int i = 0; i < first; ++i) {
            shape.steps.push_back(box(StairPart::Step, -half, half, i * g, (i + 1) * g, 0.0, (i + 1) * input.riser));
        }
        shape.steps.push_back(box(StairPart::Landing, -half, half, landingV, landingV + w, 0.0, (first + 1) * input.riser));
        for (int j = 0; j < second; ++j) {
            shape.steps.push_back(box(StairPart::Step, half + j * g, half + (j + 1) * g, landingV, landingV + w, 0.0,
                                      (first + 2 + j) * input.riser));
        }
        shape.walkLine = {{0.0, 0.0}, {0.0, landingV + half}, {half + second * g, landingV + half}};
        return shape;
    }
};

const StairTypeRegistrar registrar(std::make_unique<LShapeStair>());

}

}
