#include "railings/RailingSupport.h"

namespace plynth {

namespace {

constexpr double kPost = 0.05;
constexpr double kTopRailHeight = 0.04;
constexpr double kBottomRailFrom = 0.06;
constexpr double kBottomRailTo = 0.1;
constexpr double kBarThickness = 0.012;
constexpr double kBarDepth = 0.04;
constexpr double kBarGap = 0.1;

// Slim metal posts with a top and a bottom rail and flat upright bars between them.
class FlatBarsStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "flat_bars"; }
    std::string_view name() const override { return "Metal bars"; }
    int sortKey() const override { return 30; }

    // The posts, both rails and the bars of every bay of one run.
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        using Role = RailingPiece::Role;
        const double h = input.height;
        RailingPiece bars;
        bars.role = Role::Infill;
        const std::vector<double> ends = railing::bayEnds(input);
        for (std::size_t i = 0; i + 1 < ends.size(); ++i) {
            for (const double u : railing::spreadBetween(ends[i] + kPost / 2.0, ends[i + 1] - kPost / 2.0, kBarGap)) {
                bars.prisms.push_back(railing::box(u - kBarThickness / 2.0, u + kBarThickness / 2.0, -kBarDepth / 2.0,
                                                   kBarDepth / 2.0, kBottomRailTo, h - kTopRailHeight));
            }
        }
        return {railing::squarePosts(input, kPost), railing::alongRun(input, Role::Rail, kPost, h - kTopRailHeight, h),
                railing::alongRun(input, Role::Rail, kBarDepth, kBottomRailFrom, kBottomRailTo), std::move(bars)};
    }
};

const RailingStyleRegistrar registrar(std::make_unique<FlatBarsStyle>());

}

}
