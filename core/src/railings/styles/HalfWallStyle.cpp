#include "railings/RailingSupport.h"

namespace plynth {

namespace {

constexpr double kWallThickness = 0.15;
constexpr double kCopingWidth = 0.2;
constexpr double kCopingHeight = 0.05;

// A solid low wall with a coping on top and no posts.
class HalfWallStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "half_wall"; }
    std::string_view name() const override { return "Solid half wall"; }
    int sortKey() const override { return 60; }
    double maxHeight() const override { return 1.5; }

    // The wall and its coping along one run.
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        using Role = RailingPiece::Role;
        const double h = input.height;
        return {railing::alongRun(input, Role::Infill, kWallThickness, 0.0, h - kCopingHeight),
                railing::alongRun(input, Role::Rail, kCopingWidth, h - kCopingHeight, h)};
    }
};

const RailingStyleRegistrar registrar(std::make_unique<HalfWallStyle>());

}

}
