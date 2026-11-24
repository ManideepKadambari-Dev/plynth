#include "plynth/core/catalog/AssetSupports.h"

#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

namespace plynth {

// Checks a list of supports.
Status checkAssetSupports(const std::vector<AssetSupport>& supports)
{
    if (supports.size() > kMaxAssetSupports) {
        return fail(ErrorCode::InvalidValue,
                    "An asset can have at most " + std::to_string(kMaxAssetSupports) + " supports.");
    }
    for (std::size_t i = 0; i < supports.size(); ++i) {
        const AssetSupport& s = supports[i];
        const std::string where = "/" + std::to_string(i);
        for (double v : {s.x0, s.y0, s.x1, s.y1, s.z, s.clearance}) {
            if (!std::isfinite(v)) return fail(ErrorCode::InvalidValue, "A support value is not a number.", where);
        }
        if (!(s.x0 < s.x1 && s.y0 < s.y1)) {
            return fail(ErrorCode::InvalidValue, "A support must run from its lower corner to its upper one.", where);
        }
        if (s.x1 - s.x0 < kMinSupportSide - 1e-9 || s.y1 - s.y0 < kMinSupportSide - 1e-9) {
            return fail(ErrorCode::InvalidValue, "A support must be at least 0.1 m on each side.", where);
        }
        if (s.clearance < kMinSupportClearance - 1e-9) {
            return fail(ErrorCode::InvalidValue, "A support needs at least 0.1 m free above it.", where);
        }
        if (s.z < 0.0) return fail(ErrorCode::InvalidValue, "A support cannot be below the asset's bottom.", where);
    }
    return {};
}

// Writes the supports as a JSON list.
Json assetSupportsToJson(const std::vector<AssetSupport>& supports)
{
    Json out = Json::array();
    for (const AssetSupport& s : supports) {
        out.push_back(Json{{"x0", s.x0}, {"y0", s.y0}, {"x1", s.x1}, {"y1", s.y1}, {"z", s.z}, {"clearance", s.clearance}});
    }
    return out;
}

// Reads a JSON list of supports, empty when it is malformed.
std::optional<std::vector<AssetSupport>> assetSupportsFromJson(const Json& json)
{
    if (!json.is_array()) return std::nullopt;
    std::vector<AssetSupport> out;
    for (const Json& entry : json) {
        if (!entry.is_object() || entry.size() != 6) return std::nullopt;
        AssetSupport s;
        const std::pair<const char*, double*> fields[] = {{"x0", &s.x0}, {"y0", &s.y0}, {"x1", &s.x1},
                                                          {"y1", &s.y1}, {"z", &s.z},   {"clearance", &s.clearance}};
        for (const auto& [name, target] : fields) {
            const auto it = entry.find(name);
            if (it == entry.end() || !it->is_number()) return std::nullopt;
            *target = it->get<double>();
        }
        out.push_back(s);
    }
    if (!checkAssetSupports(out).ok()) return std::nullopt;
    return out;
}

}
