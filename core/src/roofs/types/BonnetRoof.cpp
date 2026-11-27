#include "plynth/core/Roofs.h"

namespace plynth {

namespace {

constexpr std::string_view kMansardRoofId = "mansard";

// A bonnet roof: a hip roof with a lower, flatter kick at the eaves. From the outline the kick rises at the second
// pitch over breakRatio of the run, then the hip carries on at the pitch to its hips and ridges. It is the
// mansard's shape with the two slopes swapped, so it roofs any outline the hip roofs; other sections get a gable.
class BonnetRoof final : public RoofType {
public:
    std::string_view id() const override { return "bonnet"; }
    std::string_view name() const override { return "Bonnet"; }
    int sortKey() const override { return 68; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return false; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesSecondPitch() const override { return true; }
    bool usesBreakRatio() const override { return true; }
    std::string_view fallback() const override { return kGableRoofId; }

    Status fits(const RoofInput& input) const override
    {
        const RoofType* mansard = findRoofType(kMansardRoofId);
        if (mansard && mansard->fits(input).ok()) return {};
        return fail(ErrorCode::InvalidValue,
                    "The slopes of a bonnet roof could not be worked out for this section, so it is built as a gable.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const RoofType* mansard = findRoofType(kMansardRoofId);
        if (!mansard) return {};
        RoofInput swapped = input;
        swapped.settings.pitch = input.settings.secondPitch;
        swapped.settings.secondPitch = input.settings.pitch;
        return mansard->build(swapped);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<BonnetRoof>());

}

}
