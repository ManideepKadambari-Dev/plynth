#include "plynth/core/catalog/BuildChoices.h"

#include "plynth/core/catalog/AssetInfo.h"

#include <algorithm>
#include <fstream>

namespace plynth {

namespace {

// The supports an asset's info.json holds, or none when it has none or cannot be read.
std::vector<AssetSupport> supportsOf(const std::filesystem::path& folder)
{
    std::ifstream in(folder / std::filesystem::path(std::string(kAssetInfoFileName)), std::ios::binary);
    if (!in) return {};
    const Json json = Json::parse(in, nullptr, false);
    if (!json.is_object()) return {};
    const auto it = json.find("supports");
    if (it == json.end()) return {};
    return assetSupportsFromJson(*it).value_or(std::vector<AssetSupport>{});
}

}

// The choices a library offers a build.
BuildChoices buildChoicesFrom(const AssetCatalog* assets, const MaterialCatalog* materials)
{
    BuildChoices out;
    if (assets) {
        for (const AssetRecord& record : assets->search(AssetQuery{})) {
            if (record.state != AssetState::Ready) continue;
            if (!(record.size.width > 0.0 && record.size.depth > 0.0 && record.size.height > 0.0)) continue;
            AssetChoice choice;
            choice.id = record.id;
            choice.categories = record.categories;
            choice.tags = record.tags;
            choice.size = {record.size.width, record.size.depth, record.size.height};
            choice.supports = supportsOf(record.folder);
            out.assets.push_back(std::move(choice));
        }
    }
    if (materials) {
        for (const MaterialRecord& record : materials->search(MaterialQuery{})) {
            if (record.state != MaterialState::Ready) continue;
            out.materials.push_back({record.id, record.groups});
        }
    }
    std::sort(out.assets.begin(), out.assets.end(), [](const AssetChoice& a, const AssetChoice& b) { return a.id < b.id; });
    std::sort(out.materials.begin(), out.materials.end(), [](const MaterialChoice& a, const MaterialChoice& b) { return a.id < b.id; });
    return out;
}

}
