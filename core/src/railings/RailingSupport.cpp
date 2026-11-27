#include "railings/RailingSupport.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth::railing {

namespace {

constexpr int kRoundSides = 8;

}

// A box as a prism: its outline across the run, raised from z0 to z1.
PartPrism box(double u0, double u1, double v0, double v1, double z0, double z1)
{
    PartPrism prism;
    prism.outline = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    prism.bottom = z0;
    prism.top = z1;
    return prism;
}

// A round upright as an eight sided prism.
PartPrism roundUpright(double u, double radius, double z0, double z1)
{
    PartPrism prism;
    for (int k = 0; k < kRoundSides; ++k) {
        const double a = (k + 0.5) * 2.0 * std::numbers::pi / kRoundSides;
        prism.outline.push_back({u + radius * std::cos(a), radius * std::sin(a)});
    }
    prism.bottom = z0;
    prism.top = z1;
    return prism;
}

// Splits the run into equal bays no longer than the post spacing and lists their ends.
std::vector<double> bayEnds(const RailingRunInput& input)
{
    const double length = std::max(input.length, 0.0);
    const int bays = std::max(1, static_cast<int>(std::ceil(length / kRailingPostSpacing - 1e-9)));
    std::vector<double> out;
    for (int k = 0; k <= bays; ++k) out.push_back(length * k / bays);
    return out;
}

// The bay ends without the end posts this run leaves to its neighbour.
std::vector<double> postPositions(const RailingRunInput& input)
{
    std::vector<double> all = bayEnds(input);
    std::vector<double> out;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (i == 0 && !input.postAtStart) continue;
        if (i + 1 == all.size() && !input.postAtEnd) continue;
        out.push_back(all[i]);
    }
    return out;
}

// Spreads the fewest places that keep every gap at most gap long.
std::vector<double> spreadBetween(double from, double to, double gap)
{
    std::vector<double> out;
    const double clear = to - from;
    if (!(gap > 0.0) || clear <= gap + 1e-9) return out;
    const int spaces = static_cast<int>(std::ceil(clear / gap - 1e-9));
    for (int k = 1; k < spaces; ++k) out.push_back(from + clear * k / spaces);
    return out;
}

// One box per post, centred on each post place.
RailingPiece squarePosts(const RailingRunInput& input, double size)
{
    RailingPiece piece;
    piece.role = RailingPiece::Role::Post;
    const double half = size / 2.0;
    for (const double u : postPositions(input)) piece.prisms.push_back(box(u - half, u + half, -half, half, 0.0, input.height));
    return piece;
}

// One box from the start of the run to its end.
RailingPiece alongRun(const RailingRunInput& input, RailingPiece::Role role, double width, double z0, double z1)
{
    RailingPiece piece;
    piece.role = role;
    piece.prisms.push_back(box(0.0, input.length, -width / 2.0, width / 2.0, z0, z1));
    return piece;
}

}
