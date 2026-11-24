#include "plynth/core/OpeningAssets.h"

#include "plynth/core/OpeningShapes.h"
#include "plynth/core/SeededChoice.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace plynth {

// How much an asset must be stretched to fill an opening.
double openingAssetStretch(const Opening& opening, const ItemSize& size)
{
    if (!(opening.width > 0.0 && opening.height > 0.0)) return std::numeric_limits<double>::infinity();
    return std::max(std::fabs(size.width / opening.width - 1.0), std::fabs(size.height / opening.height - 1.0));
}

// Picks the asset an opening gets.
std::optional<std::string> chooseOpeningAsset(const Opening& opening, bool autoPick,
                                              const std::vector<AssetChoice>& assets, std::uint32_t seed)
{
    if (opening.frame && !opening.frame->empty()) return opening.frame;
    if (!autoPick || opening.shape != kRectangleShapeId) return std::nullopt;
    std::string_view category;
    if (opening.type == OpeningType::Door) category = kDoorCategory;
    else if (opening.type == OpeningType::Window) category = kWindowCategory;
    else return std::nullopt;

    struct Fit {
        std::string id;
        double stretch;
    };
    std::vector<Fit> fits;
    for (const AssetChoice& a : assets) {
        if (std::find(a.categories.begin(), a.categories.end(), category) == a.categories.end()) continue;
        const double stretch = openingAssetStretch(opening, a.size);
        if (stretch <= kMaxOpeningAssetStretch + 1e-9) fits.push_back({a.id, stretch});
    }
    if (fits.empty()) return std::nullopt;
    if (seed == 0) {
        const auto best = std::min_element(fits.begin(), fits.end(), [](const Fit& a, const Fit& b) {
            if (std::fabs(a.stretch - b.stretch) > 1e-12) return a.stretch < b.stretch;
            return a.id < b.id;
        });
        return best->id;
    }
    std::vector<std::string> ids;
    for (const Fit& f : fits) ids.push_back(f.id);
    const auto index = seededChoice(seed, opening.id + "/frame", ids);
    return ids[*index];
}

// Places an asset into an opening.
OpeningAssetBuild openingAssetBuild(const OpeningBuild& opening, double elevation, std::string_view asset,
                                    const ItemSize& size, bool outsideOnRight)
{
    OpeningAssetBuild out;
    out.asset = std::string(asset);
    out.location = {opening.centre.x, opening.centre.y, elevation + opening.sill};

    bool frontRight = outsideOnRight;
    if (opening.type == OpeningType::Door) frontRight = opening.swing == WallSide::Right;
    const Vec2 front = frontRight ? opening.right : Vec2{-opening.right.x, -opening.right.y};
    // The asset's front is its -Y, which a turn of t about Z carries to (sin t, -cos t).
    out.rotationZ = std::atan2(front.x, -front.y);

    double sx = size.width > 0.0 ? opening.width / size.width : 1.0;
    const double sz = size.height > 0.0 ? opening.height / size.height : 1.0;
    if (opening.type == OpeningType::Door) {
        // Seen from the front, the viewer looks along -front and their left is that direction turned a quarter
        // anticlockwise. The asset's leaf hangs there; a hinge on the other jamb mirrors it.
        const Vec2 look{-front.x, -front.y};
        const Vec2 viewerLeft{-look.y, look.x};
        const Vec2 hinge = opening.hinge == HingeEnd::Start ? Vec2{-opening.tangent.x, -opening.tangent.y} : opening.tangent;
        if (hinge.x * viewerLeft.x + hinge.y * viewerLeft.y < 0.0) sx = -sx;
    }
    out.scale = {sx, 1.0, sz};
    return out;
}

}
