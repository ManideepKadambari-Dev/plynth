#pragma once

#include "../LicenseReport.h"
#include "AssetCatalog.h"
#include "MaterialCatalog.h"

#include <filesystem>
#include <string>
#include <vector>

namespace plynth {

// Looks up every used asset and material in the catalogs: name, source, author and license. An item that is
// not in its catalog, or a catalog that is null, gives inLibrary false and the license "unknown". The author
// comes from the item's info.json.
std::vector<LicensedItem> licensedItems(const UsedItems& used, const AssetCatalog* assets, const MaterialCatalog* materials);

struct LicenseReportRead {
    Status status; // NotFound "Build the project first." without build/used-items.json, or the read error
    LicenseReport report;
};

// The license report of a project's last build: build/used-items.json looked up in the catalogs.
LicenseReportRead projectLicenseReport(const std::filesystem::path& projectFolder, const std::string& projectName,
                                       const AssetCatalog* assets, const MaterialCatalog* materials);

}
