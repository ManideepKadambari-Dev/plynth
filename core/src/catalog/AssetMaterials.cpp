#include "plynth/core/catalog/AssetMaterials.h"

#include "plynth/core/ProjectFiles.h"

#include <algorithm>
#include <utility>

namespace plynth {

namespace fs = std::filesystem;

// Reads the asset and makes the request that takes its material, with the asset's source and license.
AssetMaterialRequest assetMaterialRequest(const LibraryPaths& library, std::string_view assetId,
                                          std::string_view materialName, std::vector<std::string> groups)
{
    AssetMaterialRequest out;
    const fs::path folder = library.assetItems / pathFromUtf8(assetId);
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        out.status = fail(ErrorCode::NotFound, "There is no asset " + std::string(assetId) + " in the library.");
        return out;
    }
    AssetInfoRead read = readAssetInfo(folder / pathFromUtf8(kAssetInfoFileName), assetId);
    if (!read.status.ok()) {
        out.status = read.status;
        return out;
    }
    out.asset = std::move(read.info);
    const auto& names = out.asset.materials;
    if (std::find(names.begin(), names.end(), materialName) == names.end()) {
        std::string known;
        for (const std::string& name : names) known += (known.empty() ? "" : ", ") + name;
        out.status = fail(ErrorCode::NotFound, "The asset " + std::string(assetId) + " has no material " +
                                                   std::string(materialName) + "." +
                                                   (known.empty() ? std::string(" It has none.") : " It has: " + known + "."));
        return out;
    }
    out.request.source.file = folder / pathFromUtf8(kAssetBlendFileName);
    out.request.blendMaterial = std::string(materialName);
    out.request.groups = std::move(groups);
    out.request.license = out.asset.license;
    out.request.origin = out.asset.source;
    return out;
}

// One request per sample folder, in name order.
std::vector<MaterialImportRequest> sampleMaterialRequests(const fs::path& from)
{
    std::vector<fs::path> folders;
    std::error_code ec;
    if (from.empty() || !fs::is_directory(from, ec)) return {};
    for (const auto& entry : fs::directory_iterator(from, ec)) {
        if (entry.is_directory(ec)) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());
    std::vector<MaterialImportRequest> requests;
    for (const fs::path& folder : folders) {
        MaterialImportRequest request;
        request.source.file = folder;
        request.skipExisting = true;
        requests.push_back(std::move(request));
    }
    return requests;
}

}
