#include "plynth/core/catalog/ItemAssets.h"

#include <optional>
#include <string>

namespace plynth {

// The plan's copy of a catalog size.
ItemSize itemSizeFrom(const AssetSize& size)
{
    return ItemSize{size.width, size.depth, size.height};
}

// Looks up an asset for placing or swapping.
ItemAsset itemAssetFor(const AssetCatalog& catalog, std::string_view assetId)
{
    ItemAsset result;
    const std::string id(assetId);
    const std::optional<AssetRecord> record = catalog.asset(assetId);
    if (!record) {
        result.status = fail(ErrorCode::NotFound, "There is no asset " + id + " in the library.");
        return result;
    }
    result.name = record->name;
    if (record->state == AssetState::Broken) {
        const std::string why = record->problem.empty() ? std::string("It is broken.") : record->problem;
        result.status = fail(ErrorCode::InvalidValue, record->name + " cannot be placed: " + why);
        return result;
    }
    if (!isMeasured(record->size)) {
        result.status = fail(ErrorCode::InvalidValue,
                             record->name + " is not measured yet. Run assets thumbnail " + id + ".");
        return result;
    }
    result.size = itemSizeFrom(record->size);
    return result;
}

// The categories the furniture dropdown offers for a room type.
std::vector<CategoryEntry> placingCategories(const AssetCatalog& catalog, std::string_view roomType)
{
    if (!roomType.empty()) {
        std::vector<CategoryEntry> linked = catalog.categoriesFor(roomType);
        if (!linked.empty()) return linked;
    }
    return catalog.categories();
}

// An AssetLookup that asks the catalog.
AssetLookup assetLookupFor(const AssetCatalog& catalog)
{
    return [&catalog](std::string_view asset) -> std::optional<ItemSize> {
        const std::optional<AssetRecord> record = catalog.asset(asset);
        if (!record) return std::nullopt;
        return itemSizeFrom(record->size);
    };
}

}
