#include "railings/RailingSupport.h"

namespace plynth {

namespace {

constexpr double kPost = 0.09;
constexpr double kTopRailHeight = 0.06;
constexpr double kBottomRailFrom = 0.05;
constexpr double kBottomRailTo = 0.1;
constexpr double kSpindleRadius = 0.016;
constexpr double kSpindleGap = 0.1;

// Wooden posts with a top and a bottom rail and round spindles between them.
class SpindlesStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "spindles"; }
    std::string_view name() const override { return "Wooden spindles"; }
    int sortKey() const override { return 20; }

    // The posts, both rails and the spindles of every bay of one run.
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        using Role = RailingPiece::Role;
        const double h = input.height;
        RailingPiece spindles;
        spindles.role = Role::Infill;
        const std::vector<double> ends = railing::bayEnds(input);
        for (std::size_t i = 0; i + 1 < ends.size(); ++i) {
            for (const double u : railing::spreadBetween(ends[i] + kPost / 2.0, ends[i + 1] - kPost / 2.0, kSpindleGap)) {
                spindles.prisms.push_back(railing::roundUpright(u, kSpindleRadius, kBottomRailTo, h - kTopRailHeight));
            }
        }
        return {railing::squarePosts(input, kPost), railing::alongRun(input, Role::Rail, 0.07, h - kTopRailHeight, h),
                railing::alongRun(input, Role::Rail, 0.05, kBottomRailFrom, kBottomRailTo), std::move(spindles)};
    }
};

const RailingStyleRegistrar registrar(std::make_unique<SpindlesStyle>());

}

}
