#pragma once

#include "../Items.h"
#include "../Plan.h"
#include "../Status.h"
#include "AssetCatalog.h"
#include "AssetInfo.h"

#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// The link between placed items and the assets catalog. Item commands never open the library; the app and the
// CLI use these before they place or swap, and to report items whose asset is missing (DESIGN.md section 7.18:
// commands validate before changing anything).

// The plan's copy of a catalog size.
ItemSize itemSizeFrom(const AssetSize& size);

// An asset that can be placed, or why it cannot.
struct ItemAsset {
    Status status;
    std::string name; // the catalog name, for messages
    ItemSize size;
};

// Looks up an asset for placing or swapping. NotFound when it is not in the catalog ("There is no asset
// sofa_01 in the library."), InvalidValue when it is Broken (the message gives its problem) or not measured
// (isMeasured is false: "Sofa is not measured yet. Run assets thumbnail sofa_01."). An asset that NeedsRefresh
// can be placed with its last measured size.
ItemAsset itemAssetFor(const AssetCatalog& catalog, std::string_view assetId);

// The categories the furniture dropdown offers for a room type, in category order: categoriesFor the room type,
// or every category when the room type is empty, unknown or has no categories linked (such as "other").
std::vector<CategoryEntry> placingCategories(const AssetCatalog& catalog, std::string_view roomType);

// An AssetLookup (Items.h) that asks the catalog. The catalog must outlive the lookup. A Broken asset counts as
// present with its last size.
AssetLookup assetLookupFor(const AssetCatalog& catalog);

}
