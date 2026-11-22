#pragma once

#include "../Blender.h"
#include "../BlenderJob.h"
#include "../Library.h"
#include "../Settings.h"
#include "../Status.h"
#include "AssetCatalog.h"
#include "Importer.h"

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Getting an asset in. Every route ends in the same steps:
// 1. Check the request (ids, names, tags, categories and license known, scale above 0, rotateZ a multiple
//    of 90) and choose the importer: request.importer when set (NotFound for an unknown id, InvalidValue when
//    it cannot read the source), otherwise pickImporter (InvalidValue naming the endings that are read when
//    none fits). Its options come from optionsForImporter (Integrations.h) and go into ImportSource::options.
// 2. For a file or folder make the id now: the request's, or catalogIdFromName of the file stem, made unique
//    with idTaken. For an address only a request id is checked now. Then make a work folder
//    assets/.staging/<job id>/ with an empty item/ folder inside, the job id made from the id, or from the
//    importer's id for an address without one.
// 3. The importer's plan. When it has a fetch: runImportFetch in the work folder with factory startup off and
//    its progress in the Blender stage, then applyFetch (StoreFetch.h). An address without a request id gets
//    its id here: catalogIdFromName of the fetched name, or the importer's id when there is none, made
//    unique. Then the cleanup_asset job in Blender (runBlenderJob, always factory startup).
// 4. Write item/info.json from the result, the request and the plan (a plan name that is not a valid name
//    gives way to one made from the id, a plan license the catalog does not have to kUnknownLicenseId),
//    rename item/ to items/<id>, index it.
// 5. Remove the original when the plan says it is a zip and nothing keeps it (moveToRecycleBin).
// 6. Delete the work folder, also after a failure, unless the failure was Blender's, in the fetch or the
//    cleanup: then the work folder is kept for the log and named in the message, and the next initLibrary
//    empties it.
// Nothing is left in items/ or the index after a failure.

enum class ImportStage {
    Preparing, // "preparing": checks and the importer's plan
    Blender,   // "blender": the fetch or cleanup job is running; step, index and count come from the worker
    Saving,    // "saving": info.json, the move into items/, the index
    Done,      // "done"
};

std::string_view importStageName(ImportStage stage);

struct ImportProgress {
    ImportStage stage = ImportStage::Preparing;
    BlenderProgress blender; // filled during the Blender stage
};

// What every import and refresh needs besides the request.
struct ImportContext {
    LibraryPaths library;
    BlenderInstall blender;   // a usable install from chooseBlender
    MachineSettings settings; // keepImportedZips, triangleWarning
    BlenderRunOptions run;
    // Removes an original zip after import. Empty: moveToRecycleBin. Tests pass a plain delete so they never
    // fill the user's Recycle Bin.
    std::function<Status(const std::filesystem::path&)> removeOriginal;
};

struct ImportOutcome {
    Status status;
    std::string assetId;
    std::string importer;                 // the importer's id
    std::vector<BlenderWarning> warnings;
    bool thumbnailFailed = false;         // imported, but the render failed; the asset shows "Render failed"
    bool originalRemoved = false;         // the zip went to the Recycle Bin
    std::filesystem::path keptWorkFolder; // set when a Blender failure kept the work folder
    // Where importInboxEntry moved the entry, done/ or failed/, with the numbered name it got. Empty when it
    // was not moved (left in the inbox, or sent to the Recycle Bin). Callers use this and never look by name.
    std::filesystem::path movedTo;
    // The plan's details, also when the plan refused (Importer::plan).
    Json details = Json::object();
};

// Imports one file, folder or address with the steps above. Blocks; the app runs it on a worker thread
// with its own AssetCatalog. Cancelled when stop is asked for; nothing is kept.
ImportOutcome importAsset(AssetCatalog& catalog, const ImportContext& context, const ImportRequest& request,
                          const std::function<void(const ImportProgress&)>& onProgress = {},
                          std::stop_token stop = {});

// The entries in assets/inbox/ waiting for import: files with an importable ending and folders, sorted by
// name, skipping done/, failed/ and names starting with "." or "~". The app waits until a file's size has
// not changed for 2 seconds before importing it, so a file still being copied is left alone.
std::vector<std::filesystem::path> inboxEntries(const LibraryPaths& library);

// Imports one inbox entry with default request fields, then moves it out of the inbox so it is never
// imported twice: a zip goes to the Recycle Bin, or to inbox/done/ when keepImportedZips is set; any other
// file or folder goes to inbox/done/. On failure it goes to inbox/failed/ with <name>.error.txt holding the
// message. A name that is already taken in done/ or failed/ gets " (2)", " (3)" and so on. An integrations.json
// that cannot be read (readIntegrationOptions fails) leaves the entry in the inbox: the entry is not at fault.
ImportOutcome importInboxEntry(AssetCatalog& catalog, const ImportContext& context,
                               const std::filesystem::path& entry,
                               const std::function<void(const ImportProgress&)>& onProgress = {},
                               std::stop_token stop = {});

// Measures an asset again, renders its thumbnail and writes asset.fbx (the render_thumbnail job with measure
// true and fbx_file set), then updates info.json and the index: used for the retry button and for assets
// that rescan lists in needsRefresh. asset.blend is not changed by that job. An asset without material maps
// (hasMaterialMaps false, every asset cleaned before those were made) is instead cleaned again: the cleanup_asset
// job with its own asset.blend as the source (format blend, scale 1, no turn) into a staging folder, whose
// asset.blend, asset.fbx, textures/ and thumb.png then replace the item's; name, categories, tags, license and
// source stay; a store asset is never fetched again for this. A failed asset.fbx is a warning only. NotFound for
// an unknown asset. When only the render fails
// the status is BlenderFailed and the asset is left Failed with the reason.
ImportOutcome refreshAsset(AssetCatalog& catalog, const ImportContext& context, std::string_view assetId,
                           const std::function<void(const ImportProgress&)>& onProgress = {},
                           std::stop_token stop = {});

}
