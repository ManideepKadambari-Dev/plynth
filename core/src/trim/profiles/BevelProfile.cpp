#include "trim/ProfileSupport.h"

namespace plynth::profiles {

namespace {

// The outer face slopes back toward the top.
class BevelProfile final : public BuiltInProfile {
public:
    BevelProfile() : BuiltInProfile(kBevelProfileId, "Bevel", 20) {}

    std::vector<Vec2> outline(double height, double depth) const override
    {
        std::vector<Vec2> out;
        appendPoint(out, {0.0, 0.0});
        appendPoint(out, {depth, 0.0});
        appendPoint(out, {depth, 0.3 * height});
        appendPoint(out, {0.5 * depth, height});
        appendPoint(out, {0.0, height});
        finishOutline(out, height, depth);
        return out;
    }
};

const TrimProfileRegistrar registrar(std::make_unique<BevelProfile>());

}

}
