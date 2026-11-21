#include "plynth/core/catalog/ItemAssets.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace plynth {

namespace {

namespace fs = std::filesystem;

// Says whether an indexed asset's folder and info.json are still on disk. The index can be older than the files
// (a folder renamed or deleted by hand), and the files are the truth.
bool filesPresent(const AssetRecord& record)
{
    std::error_code ec;
    return fs::is_directory(record.folder, ec) && fs::is_regular_file(record.folder / std::string(kAssetInfoFileName), ec);
}

}

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
    if (!filesPresent(*record)) {
        result.status = fail(ErrorCode::NotFound, "There is no asset " + id + " in the library. Its folder or info.json is gone.");
        return result;
    }
    if (record->state == AssetState::Broken) {
        const std::string why = record->problem.empty() ? std::string("It is broken.") : record->problem;
        result.status = fail(ErrorCode::InvalidValue, record->name + " cannot be placed: " + why);
        return result;
    }
    if (!readAssetInfo(record->folder / std::string(kAssetInfoFileName), id).status.ok()) {
        result.status = fail(ErrorCode::InvalidValue, record->name + " cannot be placed: its info.json cannot be read.");
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
        if (!record || !filesPresent(*record)) return std::nullopt;
        return itemSizeFrom(record->size);
    };
}

}
