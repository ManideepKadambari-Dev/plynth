#include "trim/ProfileSupport.h"

namespace plynth::profiles {

namespace {

// The ellipse touching the four sides of the box, for railings.
class RoundProfile final : public BuiltInProfile {
public:
    RoundProfile() : BuiltInProfile(kRoundProfileId, "Round", 60) {}

    std::vector<Vec2> outline(double height, double depth) const override
    {
        // A multiple of four puts a point on each side of the box.
        int n = std::max(16, ellipseSegments(depth / 2.0, height / 2.0, 2.0 * kPi));
        n = (n + 3) / 4 * 4;
        std::vector<Vec2> out;
        appendEllipseArc(out, {depth / 2.0, height / 2.0}, depth / 2.0, height / 2.0, 0.0, 2.0 * kPi, n);
        finishOutline(out, height, depth);
        return out;
    }
};

const TrimProfileRegistrar registrar(std::make_unique<RoundProfile>());

}

}
