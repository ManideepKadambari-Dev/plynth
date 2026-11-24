#include "plynth/core/Stairs.h"
#include "stairs/StairSupport.h"

namespace plynth::stairs {

namespace {

// A flight, a wide landing, then a flight coming back beside the first.
class UShapeStair final : public BuiltInStair {
public:
    UShapeStair() : BuiltInStair(kUShapeStairId, "U shape", 30, true) {}

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
        const double secondLeft = half + kUStairGap;

        StairShape shape;
        for (int i = 0; i < first; ++i) {
            shape.steps.push_back(box(StairPart::Step, -half, half, i * g, (i + 1) * g, 0.0, (i + 1) * input.riser));
        }
        shape.steps.push_back(
            box(StairPart::Landing, -half, secondLeft + w, landingV, landingV + w, 0.0, (first + 1) * input.riser));
        for (int j = 0; j < second; ++j) {
            shape.steps.push_back(box(StairPart::Step, secondLeft, secondLeft + w, landingV - (j + 1) * g, landingV - j * g,
                                      0.0, (first + 2 + j) * input.riser));
        }
        const double walkU = w + kUStairGap;
        shape.walkLine = {{0.0, 0.0}, {0.0, landingV + half}, {walkU, landingV + half}, {walkU, landingV - second * g}};
        return shape;
    }
};

const StairTypeRegistrar registrar(std::make_unique<UShapeStair>());

}

}
