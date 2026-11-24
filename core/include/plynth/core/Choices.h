#pragma once

#include "Plan.h"
#include "catalog/AssetSupports.h"

#include <string>
#include <vector>

namespace plynth {

// What the library offers a build to choose from: catalog doors and windows, varied furniture, set dressing
// props and varied materials. Made from the catalogs (buildChoicesFrom, from milestone 15 part B). Only usable
// entries are listed: assets that are ready and measured, materials that are ready. Each list is sorted by id.

// One asset a build may choose.
struct AssetChoice {
    std::string id;
    std::vector<std::string> categories;
    std::vector<std::string> tags;
    ItemSize size;
    std::vector<AssetSupport> supports;

    bool operator==(const AssetChoice&) const = default;
};

// One material a build may choose. groups are the material's catalog groups, for example "floor" or "wall".
struct MaterialChoice {
    std::string id;
    std::vector<std::string> groups;

    bool operator==(const MaterialChoice&) const = default;
};

struct BuildChoices {
    std::vector<AssetChoice> assets;
    std::vector<MaterialChoice> materials;

    bool operator==(const BuildChoices&) const = default;
};

}
