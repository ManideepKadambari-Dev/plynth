#pragma once

#include "BuildGeometry.h"
#include "Choices.h"
#include "Plan.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Catalog doors and windows fitted into openings (DESIGN.md section 7.13a). An opening's frame names an asset
// and always wins. Otherwise, when picking is on, a rectangular door or window gets an asset of category
// "door" or "window" whose width and height need at most kMaxOpeningAssetStretch of stretch. Other shapes,
// plain openings and openings with no fitting asset keep the fill Plynth builds.
//
// Conventions for door and window assets: the front faces -Y like every asset, a door's leaf hangs on its
// left jamb seen from the front, and the asset's size is the size of the hole it fills.

inline constexpr double kMaxOpeningAssetStretch = 0.1;

inline constexpr std::string_view kDoorCategory = "door";
inline constexpr std::string_view kWindowCategory = "window";

// Where an opening's asset goes in the world: its pivot (bottom centre) at location, turned about Z by
// rotationZ (radians), scaled by scale along its own axes. scale.x below zero mirrors it, for a door that
// hangs on the other jamb.
struct OpeningAssetBuild {
    std::string asset;
    Vec3 location;
    double rotationZ = 0.0;
    Vec3 scale{1.0, 1.0, 1.0};

    bool operator==(const OpeningAssetBuild&) const = default;
};

// How much an asset of this size must be stretched to fill an opening: the larger of |width / opening width - 1|
// and |height / opening height - 1|, so 0.1 is 10 percent.
double openingAssetStretch(const Opening& opening, const ItemSize& size);

// The asset an opening gets: its frame when set, whatever the choices hold. Otherwise, when autoPick is on and
// the opening is a rectangular door or window, the asset of its category (kDoorCategory or kWindowCategory)
// with the least stretch, at most kMaxOpeningAssetStretch (with 1e-9 to spare); with seed 0 the least stretch
// wins and ties go to the smaller id, with another seed seededChoice picks among all that fit, keyed by the
// opening id. Empty when none applies.
std::optional<std::string> chooseOpeningAsset(const Opening& opening, bool autoPick,
                                              const std::vector<AssetChoice>& assets, std::uint32_t seed);

// Places an asset of a size into an opening of a floor at a level, as the build makes it. The pivot goes to
// the bottom centre of the opening on the wall's centre line: OpeningBuild::centre at Z = elevation plus the
// sill. Width and height are stretched to the opening, depth is kept.
// A door's front faces its swing side, and it is mirrored (scale.x below zero) when its hinge jamb is on the
// right seen from that side. A window's front faces outside: outsideOnRight is true when the wall's right side
// looks into no room, or when both sides or neither do, so a window between two rooms faces right.
OpeningAssetBuild openingAssetBuild(const OpeningBuild& opening, double elevation, std::string_view asset,
                                    const ItemSize& size, bool outsideOnRight);

}
