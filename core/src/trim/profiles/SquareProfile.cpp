#include "trim/ProfileSupport.h"

namespace plynth::profiles {

namespace {

// The box with a small chamfer on its outer top corner.
class SquareProfile final : public BuiltInProfile {
public:
    SquareProfile() : BuiltInProfile(kSquareProfileId, "Square", 10) {}

    std::vector<Vec2> outline(double height, double depth) const override
    {
        const double c = std::min(0.003, 0.5 * std::min(height, depth));
        std::vector<Vec2> out;
        appendPoint(out, {0.0, 0.0});
        appendPoint(out, {depth, 0.0});
        appendPoint(out, {depth, height - c});
        appendPoint(out, {depth - c, height});
        appendPoint(out, {0.0, height});
        finishOutline(out, height, depth);
        return out;
    }
};

const TrimProfileRegistrar registrar(std::make_unique<SquareProfile>());

}

}
