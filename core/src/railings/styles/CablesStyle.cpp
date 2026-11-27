#include "railings/RailingSupport.h"

namespace plynth {

namespace {

constexpr double kPost = 0.05;
constexpr double kTopRailHeight = 0.04;
constexpr double kCable = 0.006;
constexpr double kLowestCable = 0.1;
constexpr double kCableBelowRail = 0.06;
constexpr double kCableGap = 0.08;

// Slim posts with a top rail and thin horizontal cables between them.
class CablesStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "cables"; }
    std::string_view name() const override { return "Horizontal cables"; }
    int sortKey() const override { return 50; }

    // The posts, the top rail and the cables of one run.
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        using Role = RailingPiece::Role;
        const double h = input.height;
        const double highest = h - kTopRailHeight - kCableBelowRail;
        std::vector<double> heights{kLowestCable};
        if (highest > kLowestCable + 1e-9) {
            for (const double z : railing::spreadBetween(kLowestCable, highest, kCableGap)) heights.push_back(z);
            heights.push_back(highest);
        }
        RailingPiece cables;
        cables.role = Role::Infill;
        for (const double z : heights) {
            cables.prisms.push_back(
                railing::box(0.0, input.length, -kCable / 2.0, kCable / 2.0, z - kCable / 2.0, z + kCable / 2.0));
        }
        return {railing::squarePosts(input, kPost), railing::alongRun(input, Role::Rail, kPost, h - kTopRailHeight, h),
                std::move(cables)};
    }
};

const RailingStyleRegistrar registrar(std::make_unique<CablesStyle>());

}

}
