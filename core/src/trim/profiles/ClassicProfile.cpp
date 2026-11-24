#include "trim/ProfileSupport.h"

namespace plynth::profiles {

namespace {

// A skirting board: a flat face, a quarter round, then a thinner top.
class ClassicProfile final : public BuiltInProfile {
public:
    ClassicProfile() : BuiltInProfile(kClassicProfileId, "Classic", 40) {}

    std::vector<Vec2> outline(double height, double depth) const override
    {
        const double r = std::min(0.5 * depth, 0.25 * height);
        const double shoulder = 0.65 * height;
        std::vector<Vec2> out;
        appendPoint(out, {0.0, 0.0});
        appendPoint(out, {depth, 0.0});
        appendEllipseArc(out, {depth - r, shoulder}, r, r, 0.0, kPi / 2.0, ellipseSegments(r, r, kPi / 2.0));
        appendPoint(out, {depth - r, height});
        appendPoint(out, {0.0, height});
        finishOutline(out, height, depth);
        return out;
    }
};

const TrimProfileRegistrar registrar(std::make_unique<ClassicProfile>());

}

}
