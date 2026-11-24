#pragma once

#include "../Status.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// A flat place on an asset where set dressing may put small props: a shelf board, a table top. Measured by
// Blender at cleanup and kept in the asset's info.json. In the asset's own axes, in metres from its pivot (the
// bottom centre): x along its width, y along its depth (back to front, Blender -Y is the front, so y here is
// the asset's -Y), z up. x0 < x1 and y0 < y1. clearance is the free height above the place.
struct AssetSupport {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z = 0.0;
    double clearance = 0.0;

    bool operator==(const AssetSupport&) const = default;
};

inline constexpr double kMinSupportSide = 0.1;
inline constexpr double kMinSupportClearance = 0.1;
inline constexpr std::size_t kMaxAssetSupports = 16;

// Checks a list of supports: every number finite, x0 < x1 and y0 < y1, both sides at least kMinSupportSide,
// clearance at least kMinSupportClearance, z zero or more, at most kMaxAssetSupports entries. InvalidValue with
// where set to the entry, for example "/2".
Status checkAssetSupports(const std::vector<AssetSupport>& supports);

// The supports as a JSON list of objects {"x0", "y0", "x1", "y1", "z", "clearance"}, the shape of
// schema/asset-supports.schema.json.
Json assetSupportsToJson(const std::vector<AssetSupport>& supports);

// Reads such a list. Empty when the value is not a list of that shape or the list breaks checkAssetSupports.
std::optional<std::vector<AssetSupport>> assetSupportsFromJson(const Json& json);

}
