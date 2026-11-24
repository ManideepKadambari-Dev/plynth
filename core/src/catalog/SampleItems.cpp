#include "plynth/core/catalog/SampleItems.h"

#include "plynth/core/Generators.h"
#include "plynth/core/Library.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Samples.h"
#include "plynth/core/catalog/AssetMaterials.h"
#include "plynth/core/catalog/MaterialImport.h"

namespace plynth {

// Imports the sample models and materials and copies the shapes and generators, skipping what is there.
SampleItemsResult addSampleItems(AssetCatalog& assets, MaterialCatalog& materials, const ImportContext& context,
                                 const std::filesystem::path& samplesFolder,
                                 const std::function<void(int index, int count)>& progress, std::stop_token stop)
{
    SampleItemsResult out;
    const SamplesRead read = readSamples(samplesFolder);
    if (!read.status.ok()) {
        out.status = fail(ErrorCode::NotFound, "The samples were not found next to the program. " + read.status.message);
        return out;
    }
    std::vector<MaterialImportRequest> materialRequests = sampleMaterialRequests(samplesFolder / L"materials");
    const int count = static_cast<int>(read.index.assets.size() + materialRequests.size()) + 2;
    int index = 0;
    const auto step = [&] {
        if (progress) progress(index, count);
        ++index;
    };

    for (const SampleAsset& sample : read.index.assets) {
        step();
        SampleItemResult item{"asset", sample.id, {}, false};
        if (stop.stop_requested()) {
            item.status = fail(ErrorCode::Cancelled, "The sample items were not all added.");
        } else if (assets.idTaken(sample.id)) {
            item.skipped = true;
        } else {
            ImportRequest request;
            request.source.file = samplesFolder / pathFromUtf8(sample.file);
            request.id = sample.id;
            request.name = sample.name;
            request.categories = sample.categories;
            request.tags = sample.tags;
            request.license = read.index.license;
            request.keepOriginal = true;
            item.status = importAsset(assets, context, request, {}, stop).status;
        }
        out.items.push_back(std::move(item));
    }

    for (MaterialImportRequest& request : materialRequests) {
        step();
        SampleItemResult item{"material", pathToUtf8(request.source.file.filename()), {}, false};
        if (stop.stop_requested()) {
            item.status = fail(ErrorCode::Cancelled, "The sample items were not all added.");
        } else {
            if (request.license.empty()) request.license = read.index.license;
            request.keepOriginal = true;
            const MaterialImportOutcome outcome = importMaterial(materials, context, request, {}, stop);
            item.status = outcome.status;
            item.skipped = outcome.skipped;
            if (!outcome.materialId.empty()) item.id = outcome.materialId;
        }
        out.items.push_back(std::move(item));
    }

    step();
    const SampleShapesCopy shapes = addSampleShapes(context.library, samplesFolder / L"shapes");
    for (const std::string& name : shapes.copied) out.items.push_back({"shape", name, {}, false});
    for (const std::string& name : shapes.skipped) out.items.push_back({"shape", name, {}, true});
    if (!shapes.status.ok() && shapes.status.code != ErrorCode::NotFound) out.items.push_back({"shape", "", shapes.status, false});

    step();
    const SampleGeneratorsCopy generators = addSampleGenerators(context.library.generators, samplesFolder / L"generators");
    for (const std::string& name : generators.copied) out.items.push_back({"generator", name, {}, false});
    for (const std::string& name : generators.skipped) out.items.push_back({"generator", name, {}, true});
    if (!generators.status.ok() && generators.status.code != ErrorCode::NotFound) {
        out.items.push_back({"generator", "", generators.status, false});
    }
    return out;
}

}
