#include "plynth/core/catalog/MaterialRescan.h"

#include <filesystem>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Says whether items/ holds a material folder: a folder whose name does not start with a dot.
bool hasMaterialFolders(const fs::path& items)
{
    std::error_code ec;
    for (fs::directory_iterator it(items, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code kind;
        if (it->is_directory(kind) && !it->path().filename().wstring().starts_with(L'.')) return true;
    }
    return false;
}

}

// Opens the materials catalog and indexes a library that was filled by hand.
std::unique_ptr<MaterialCatalog> openMaterialCatalog(const LibraryPaths& library, Status& status)
{
    std::unique_ptr<MaterialCatalog> catalog = MaterialCatalog::open(library, status);
    if (catalog && catalog->materialCount() == 0 && hasMaterialFolders(library.materialItems)) catalog->rescan();
    return catalog;
}

// Rebuilds the materials index and renders again every material that changed.
MaterialRescanResult rescanMaterials(MaterialCatalog& catalog, const ImportContext& context, bool refresh,
                                     const std::function<void(int index, int count)>& progress, std::stop_token stop)
{
    MaterialRescanResult result;
    result.report = catalog.rescan();
    if (!refresh) return result;
    const int count = static_cast<int>(result.report.needsRefresh.size());
    for (int index = 0; index < count; ++index) {
        const std::string& id = result.report.needsRefresh[static_cast<std::size_t>(index)];
        if (progress) progress(index, count);
        MaterialRefreshResult entry{id, {}};
        if (stop.stop_requested()) {
            entry.status = fail(ErrorCode::Cancelled, "The material " + id + " was not rendered again.");
        } else {
            entry.status = refreshMaterial(catalog, context, id, {}, stop).status;
        }
        result.refreshed.push_back(std::move(entry));
    }
    return result;
}

}
