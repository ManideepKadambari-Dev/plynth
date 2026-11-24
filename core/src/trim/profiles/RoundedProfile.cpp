#include "trim/ProfileSupport.h"

namespace plynth::profiles {

namespace {

// The outer top corner rounded with a radius of half the smaller side.
class RoundedProfile final : public BuiltInProfile {
public:
    RoundedProfile() : BuiltInProfile(kRoundedProfileId, "Rounded", 30) {}

    std::vector<Vec2> outline(double height, double depth) const override
    {
        const double r = 0.5 * std::min(height, depth);
        std::vector<Vec2> out;
        appendPoint(out, {0.0, 0.0});
        appendPoint(out, {depth, 0.0});
        appendEllipseArc(out, {depth - r, height - r}, r, r, 0.0, kPi / 2.0, ellipseSegments(r, r, kPi / 2.0));
        appendPoint(out, {0.0, height});
        finishOutline(out, height, depth);
        return out;
    }
};

const TrimProfileRegistrar registrar(std::make_unique<RoundedProfile>());

}

}
