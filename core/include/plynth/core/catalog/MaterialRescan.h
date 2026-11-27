#pragma once

#include "../Library.h"
#include "../Status.h"
#include "AssetCatalog.h"
#include "AssetImport.h"
#include "MaterialCatalog.h"
#include "MaterialImport.h"

#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace plynth {

// The materials catalog's rescan and opening, the same in the app and the CLI (issue 46).

// Opens the materials catalog; when its index is empty and items/ has folders (a library filled by hand), runs
// rescan once before returning, so "plynth materials list" and the app show the same materials.
std::unique_ptr<MaterialCatalog> openMaterialCatalog(const LibraryPaths& library, Status& status);

// One material that rescan sent to refreshMaterial, and what happened.
struct MaterialRefreshResult {
    std::string id;
    Status status;
};

struct MaterialRescanResult {
    RescanReport report;                          // MaterialCatalog::rescan
    std::vector<MaterialRefreshResult> refreshed; // one per id of report.needsRefresh, in its order
};

// The Rescan button and "plynth materials rescan": rescan, then refreshMaterial for every material it lists in
// needsRefresh, which renders its thumbnail again. With refresh false only rescan runs, as with
// "plynth materials rescan --no-refresh". Blocks.
MaterialRescanResult rescanMaterials(MaterialCatalog& catalog, const ImportContext& context, bool refresh,
                                     const std::function<void(int index, int count)>& progress = {},
                                     std::stop_token stop = {});

}
