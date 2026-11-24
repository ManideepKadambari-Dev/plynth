#pragma once

#include "../Library.h"
#include "../Status.h"
#include "AssetInfo.h"
#include "MaterialImport.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Taking one material of an asset into the materials catalog: the Assets panel's "Add to materials catalog" and
// "plynth materials from-asset". It imports the material from the asset's asset.blend, and the new material
// keeps the asset's source and license, so the material of a store asset stays a store item (Pack.h).
struct AssetMaterialRequest {
    Status status; // NotFound for an unknown asset or a material the asset does not have, the errors of readAssetInfo
    MaterialImportRequest request;
    AssetInfo asset;
};

// The import request for material materialName of the asset assetId, with these groups. Nothing is imported yet;
// the caller runs importMaterial with the request, on its own thread when it likes.
AssetMaterialRequest assetMaterialRequest(const LibraryPaths& library, std::string_view assetId,
                                          std::string_view materialName, std::vector<std::string> groups = {});

// The sample materials (samples/materials): one import request for each of its folders, in name order, each with
// skipExisting set, so a material the library has is skipped and keeps its id. The Materials panel's "Add the
// sample materials" and "plynth library add-sample-materials" both run these. Empty when the folder is missing.
std::vector<MaterialImportRequest> sampleMaterialRequests(const std::filesystem::path& from);

}
