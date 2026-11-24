#include "trim/ProfileSupport.h"

namespace plynth::profiles {

namespace {

// A cornice: a concave quarter ellipse from the wall at the bottom to the ceiling at the front.
class CoveProfile final : public BuiltInProfile {
public:
    CoveProfile() : BuiltInProfile(kCoveProfileId, "Cove", 50) {}

    std::vector<Vec2> outline(double height, double depth) const override
    {
        std::vector<Vec2> out;
        appendEllipseArc(out, {depth, 0.0}, depth, height, kPi, kPi / 2.0, ellipseSegments(depth, height, kPi / 2.0));
        appendPoint(out, {0.0, height});
        finishOutline(out, height, depth);
        return out;
    }
};

const TrimProfileRegistrar registrar(std::make_unique<CoveProfile>());

}

}
