#include "plynth/core/catalog/LicensedItems.h"

#include "plynth/core/Build.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/MaterialInfo.h"

namespace plynth {

namespace {

// The license entry with this id from a list, or one that names the id when the list does not have it.
LicenseEntry licenseFor(const std::vector<LicenseEntry>& list, const std::string& id)
{
    for (const LicenseEntry& entry : list) {
        if (entry.id == id) return entry;
    }
    LicenseEntry unknown;
    unknown.id = id.empty() ? std::string(kUnknownLicenseId) : id;
    unknown.name = unknown.id == kUnknownLicenseId ? "Unknown" : unknown.id;
    return unknown;
}

// An item that is not in its catalog.
LicensedItem missingItem(LicensedKind kind, const std::string& id)
{
    LicensedItem item;
    item.kind = kind;
    item.id = id;
    item.inLibrary = false;
    item.license.id = std::string(kUnknownLicenseId);
    item.license.name = "Unknown";
    return item;
}

}

// Looks up each used id in its catalog.
std::vector<LicensedItem> licensedItems(const UsedItems& used, const AssetCatalog* assets, const MaterialCatalog* materials)
{
    std::vector<LicensedItem> items;
    const std::vector<LicenseEntry> assetLicenses = assets ? assets->licenses() : std::vector<LicenseEntry>{};
    for (const std::string& id : used.assets) {
        const std::optional<AssetRecord> record = assets ? assets->asset(id) : std::nullopt;
        if (!record) {
            items.push_back(missingItem(LicensedKind::Asset, id));
            continue;
        }
        LicensedItem item;
        item.kind = LicensedKind::Asset;
        item.id = record->id;
        item.name = record->name;
        item.sourceKind = record->sourceKind;
        item.sourceReference = record->sourceReference;
        item.importedAt = record->importedAt;
        item.license = licenseFor(assetLicenses, record->license);
        const AssetInfoRead info = readAssetInfo(record->folder / pathFromUtf8(kAssetInfoFileName), record->id);
        if (info.status.ok()) item.author = info.info.author;
        items.push_back(std::move(item));
    }
    const std::vector<LicenseEntry> materialLicenses = materials ? materials->licenses() : std::vector<LicenseEntry>{};
    for (const std::string& id : used.materials) {
        const std::optional<MaterialRecord> record = materials ? materials->material(id) : std::nullopt;
        if (!record) {
            items.push_back(missingItem(LicensedKind::Material, id));
            continue;
        }
        LicensedItem item;
        item.kind = LicensedKind::Material;
        item.id = record->id;
        item.name = record->name;
        item.sourceKind = record->sourceKind;
        item.sourceReference = record->sourceReference;
        item.importedAt = record->importedAt;
        item.license = licenseFor(materialLicenses, record->license);
        const MaterialInfoRead info = readMaterialInfo(record->folder / pathFromUtf8(kMaterialInfoFileName), record->id);
        if (info.status.ok()) item.author = info.info.author;
        items.push_back(std::move(item));
    }
    return items;
}

// Reads the used items of the last build and makes the report from them.
LicenseReportRead projectLicenseReport(const std::filesystem::path& projectFolder, const std::string& projectName,
                                       const AssetCatalog* assets, const MaterialCatalog* materials)
{
    LicenseReportRead out;
    const UsedItemsRead used = readUsedItems(buildFolder(projectFolder) / pathFromUtf8(kUsedItemsFileName));
    if (!used.status.ok()) {
        out.status = used.status;
        return out;
    }
    out.report = makeLicenseReport(projectName, used.items.planFingerprint, licensedItems(used.items, assets, materials));
    return out;
}

}
