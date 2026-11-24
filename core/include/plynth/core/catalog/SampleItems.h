#pragma once

#include "../Status.h"
#include "AssetCatalog.h"
#include "AssetImport.h"
#include "MaterialCatalog.h"

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace plynth {

// One sample item and what happened to it. kind is "asset", "material", "shape" or "generator".
struct SampleItemResult {
    std::string kind;
    std::string id;
    Status status;
    bool skipped = false; // the library had it already and it was left as it is
};

struct SampleItemsResult {
    Status status; // NotFound without a samples index; otherwise Ok, even when some items failed
    std::vector<SampleItemResult> items;
};

// Adds the samples a library does not have yet: every model of samples.json with its categories, tags and the
// index's license (cc0), every sample material (sampleMaterialRequests of AssetMaterials.h), and copies of the
// sample shapes and generators. The sample files are only read. An id the library has already is skipped and
// keeps its own license. A failed item is listed and the rest go on. progress gets the index of the item being
// worked on and how many there are.
SampleItemsResult addSampleItems(AssetCatalog& assets, MaterialCatalog& materials, const ImportContext& context,
                                 const std::filesystem::path& samplesFolder,
                                 const std::function<void(int index, int count)>& progress = {},
                                 std::stop_token stop = {});

}
