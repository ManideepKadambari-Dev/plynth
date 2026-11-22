#pragma once

#include "../Blender.h"
#include "../BlenderJob.h"
#include "../Status.h"
#include "ImportFetch.h"
#include "Importer.h"
#include "MaterialImporter.h"

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace plynth {

// Running the fetch job of ImportFetch.h and handing what it brought to the cleanup.
//
// Every fetch task, whatever the store, writes into params.output_folder and reports the same output
// (schema/blender-result.schema.json "fetchOutput"):
//   {"file": "Wooden_chair_2K.blend", "format": "blend", "kind": "model", "name": "Wooden chair" or null,
//    "author": "Some Author" or null, "license": "royalty_free" or null,
//    "reference": "https://www.blenderkit.com/asset-gallery-detail/<id>/" or null, "material": null}
// file is relative to output_folder. format is a cleanup_asset format (AssetJobs.h) for a model and a worker
// material source (MaterialImporter.h) for a material; both are "blend" for BlenderKit. license is a license
// id of the catalogs or null when the store's license is not known. material names the material inside file
// for a material fetch, null otherwise.
//
// In the imports: importAsset and importMaterial run the plan's fetch, when it has one, after the importer's
// plan and before the cleanup job, in the work folder of the import, with progress in the Blender stage, and
// then call applyFetch. The two jobs share job.json, result.json and blender.log in that folder, one after the
// other. A failed fetch keeps the work folder like a failed cleanup. Its log holds only the steps of
// ImportFetch::steps, never what Blender or an add-on printed.
//
// A fetch task's failure is a fixed sentence, to which it may add the add-on's version (digits, dots and
// hyphens). It
// never puts an add-on's output, log text, exception text or other values into a result, at most an
// exception's type name, also for a failure it did not expect.
// A refusal by applyFetch is not Blender's failure, so the work folder is removed.

struct FetchOutput {
    BlenderResultHeader header;
    std::string file;      // relative to the fetch's output folder
    std::string format;
    std::string kind;      // kFetchKindModel or kFetchKindMaterial
    std::string name;      // empty when the store gave none
    std::string author;
    std::string license;   // a license id, or empty
    std::string reference; // the item's page address or the store's id for it, or empty
    std::string material;  // material fetches only
};

// Reads and checks a fetch result. BadFile when a field is missing, kind is unknown, or file is absolute or
// leaves the output folder. When ok is false the header holds the worker's error code and message.
FetchOutput readFetchResult(const Json& result, std::string_view task, std::string_view jobId);

struct FetchRun {
    // Ok when the file is there. BlenderFailed with the worker's message and errorCode when the task failed
    // (BlenderKit.h lists its codes), BlenderFailed when the reported file does not exist or the timeout
    // passed, Cancelled on stop.
    Status status;
    std::string errorCode;
    FetchOutput output;
    std::filesystem::path file; // absolute: the output folder / output.file
};

// Runs a fetch: makes workFolder/kFetchOutputFolder, writes the job with output_folder set, and runs it with
// runBlenderJob with factory startup off, base's scripts folder, and fetch.timeout. Always with --background. Blocks.
FetchRun runImportFetch(const ImportFetch& fetch, const std::filesystem::path& blender,
                        const BlenderRunOptions& base, const std::filesystem::path& workFolder,
                        const std::function<void(const BlenderProgress&)>& onProgress = {},
                        std::stop_token stop = {});

// Puts what a fetch brought into an asset plan: file, format, and defaultName, license and reference where
// the plan has none. InvalidValue when the fetched kind is not the plan's expected kind, for example "This
// BlenderKit item is a material. Import it in the Materials tab or with plynth materials import."
// The BlenderKit task itself already fails such an item with wrong_asset_kind and the same sentence
// (BlenderFailed); this check is for a task that reports the other kind as a success.
Status applyFetch(const FetchRun& run, const ImportFetch& fetch, ImportPlan& plan);

// The same for a material plan: path, workerSource from format, blendMaterial from material, and the
// defaults. The asset sentence is "This BlenderKit item is a model. Import it in the Assets tab or with
// plynth assets import."
Status applyFetch(const FetchRun& run, const ImportFetch& fetch, MaterialImportPlan& plan);

}
