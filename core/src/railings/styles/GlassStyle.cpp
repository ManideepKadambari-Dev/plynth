#include "railings/RailingSupport.h"

#include <algorithm>
#include <cmath>

namespace plynth {

namespace {

constexpr double kShoeWidth = 0.08;
constexpr double kShoeHeight = 0.12;
constexpr double kGlassFoot = 0.04;   // how high the glass starts inside the shoe
constexpr double kRailWidth = 0.04;
constexpr double kRailHeight = 0.03;
constexpr double kGlassInRail = 0.02; // how far the glass reaches up into the top rail
constexpr double kMaxPanelWidth = 1.2;
constexpr double kPanelGap = 0.01;

// Glass panels standing in a base shoe, with a slim rail on top.
class GlassStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "glass"; }
    std::string_view name() const override { return "Glass panels"; }
    int sortKey() const override { return 40; }
    double maxHeight() const override { return 1.5; }

    // The shoe, the panels and the top rail of one run.
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        using Role = RailingPiece::Role;
        const double h = input.height;
        const double l = input.length;
        RailingPiece glass;
        glass.role = Role::Glass;
        const int panels = std::max(1, static_cast<int>(std::ceil(l / kMaxPanelWidth - 1e-9)));
        for (int i = 0; i < panels; ++i) {
            const double from = l * i / panels + (i > 0 ? kPanelGap / 2.0 : 0.0);
            const double to = l * (i + 1) / panels - (i + 1 < panels ? kPanelGap / 2.0 : 0.0);
            glass.prisms.push_back(railing::box(from, to, -kGlassThickness / 2.0, kGlassThickness / 2.0, kGlassFoot,
                                                h - kRailHeight + kGlassInRail));
        }
        return {railing::alongRun(input, Role::Shoe, kShoeWidth, 0.0, kShoeHeight), std::move(glass),
                railing::alongRun(input, Role::Rail, kRailWidth, h - kRailHeight, h)};
    }
};

const RailingStyleRegistrar registrar(std::make_unique<GlassStyle>());

}

}
