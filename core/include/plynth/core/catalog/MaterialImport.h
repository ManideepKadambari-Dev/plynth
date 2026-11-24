#pragma once

#include "../BlenderJob.h"
#include "../Library.h"
#include "../Status.h"
#include "AssetImport.h"
#include "Importer.h"
#include "MaterialCatalog.h"
#include "MaterialImporter.h"
#include "MaterialJobs.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

// Getting a material in, the same steps as importAsset in AssetImport.h with the materials catalog and its
// own folders:
// 1. Check the request (a file or an address, ids, names, tags, groups and license known, tile size by
//    checkTileSize, values from 0 to 1, files that exist, bake size) and choose the importer: request.importer
//    when set, otherwise pickMaterialImporter. Its options come from optionsForImporter (Integrations.h) and
//    go into ImportSource::options.
// 2. Make a work folder materials/.staging/<job id>/ with an empty item/ folder inside. A request id is checked
//    now: AlreadyExists when it is taken, or skipped when skipExisting is set.
// 3. The importer's plan. When it has a fetch: runImportFetch in the work folder with factory startup off and
//    its progress in the Blender stage, then applyFetch (StoreFetch.h). Then the cleanup_material job
//    (runBlenderJob, always factory startup) with the request's blendMaterial, or the plan's when that is
//    empty, and the request's maps, values, normal, tile size and bake size whatever the importer. The worker
//    names its map files by channel, so it does not need the id.
// 4. Pick the id: the request's, then material.json's, then catalogIdFromName of the name. A material.json or
//    name id that is taken is made unique with uniqueCatalogId, or skipped when skipExisting is set (the
//    sample materials must keep their ids, which project defaults use). Write item/info.json from the result,
//    the request and material.json (request first, then material.json, then the plan's defaults; for the name,
//    the picked material's comes after those, so a store's name wins over the one inside the file; a plan
//    license the catalog does not have becomes kUnknownLicenseId), rename item/ to items/<id>, index it.
// 5. Remove the original when it is a zip and nothing keeps it.
// 6. Delete the work folder, unless Blender failed in the fetch or the cleanup: then it is kept for the log
//    and named in the message.
// Nothing is left in items/ or the index after a failure. A material that came with an imported asset stays
// with the asset; it only comes into this catalog through an explicit import of the asset's asset.blend with
// the material named.

struct MaterialImportRequest {
    ImportSource source;
    std::string id;
    std::string name;
    std::vector<std::string> groups;
    std::vector<std::string> tags;
    std::string license;
    std::optional<TileSize> tileSize;
    std::string blendMaterial; // the material to take from a .blend; empty for the only one
    std::vector<std::pair<MapChannel, std::filesystem::path>> maps;
    std::vector<std::pair<MapChannel, double>> values;
    NormalConvention normal = NormalConvention::Auto;
    int bakeSize = kDefaultBakeSize;
    std::optional<bool> keepOriginal; // empty: the keepImportedZips setting
    bool skipExisting = false;        // an id that is taken skips the import instead of making a new id
    // Written as the source in info.json instead of the import's own. A material taken from an asset keeps the
    // asset's source this way, so a store asset's material stays a store item (AssetMaterials.h).
    std::optional<AssetSource> origin;
    // As in ImportRequest: the importer by id (empty: pickMaterialImporter) and options for this import
    // only, checked by the importer's integration.
    std::string importer;
    Json importerOptions = Json::object();
};

struct MaterialImportOutcome {
    Status status;
    std::string materialId;
    bool skipped = false; // skipExisting found the id taken; status is ok and nothing was added
    std::string importer;
    std::vector<BlenderWarning> warnings;
    bool thumbnailFailed = false;
    bool originalRemoved = false;
    std::filesystem::path keptWorkFolder;
    std::filesystem::path movedTo; // as in ImportOutcome
    Json details = Json::object(); // as in ImportOutcome
};

// Imports one folder, file, zip or address with the steps above. Blocks; the app runs it on a worker thread with its own
// MaterialCatalog. Cancelled when stop is asked for; nothing is kept. Progress reuses ImportProgress.
MaterialImportOutcome importMaterial(MaterialCatalog& catalog, const ImportContext& context,
                                     const MaterialImportRequest& request,
                                     const std::function<void(const ImportProgress&)>& onProgress = {},
                                     std::stop_token stop = {});

// The entries in materials/inbox/ waiting for import: folders, zips, image files and .blend files, sorted by
// name, skipping done/, failed/ and names starting with "." or "~". The app waits until a file's size has not
// changed for 2 seconds, as for assets.
std::vector<std::filesystem::path> materialInboxEntries(const LibraryPaths& library);

// Imports one inbox entry with default request fields, then moves it out of the inbox by the rules of
// importInboxEntry (done/, failed/ with <name>.error.txt, a zip to the Recycle Bin unless kept, left in the
// inbox when integrations.json cannot be read).
MaterialImportOutcome importMaterialInboxEntry(MaterialCatalog& catalog, const ImportContext& context,
                                               const std::filesystem::path& entry,
                                               const std::function<void(const ImportProgress&)>& onProgress = {},
                                               std::stop_token stop = {});

// Cleans a material again and renders its thumbnail: the retry button, materials rescan lists in needsRefresh,
// and a changed tile size. Runs cleanup_material with source texture_set on the item folder, its current maps
// named in maps (none for a folder that was never cleaned), its tile size, into a work folder; then moves the
// new maps and thumb.png into the item folder, removes the old map files info.json named that were not written
// again, and writes info.json, keeping name, groups, tags, license and source. Other files in the folder are
// never deleted. NotFound for an unknown material. When only
// the render fails the status is BlenderFailed and the material is left Failed with the reason.
MaterialImportOutcome refreshMaterial(MaterialCatalog& catalog, const ImportContext& context,
                                      std::string_view materialId,
                                      const std::function<void(const ImportProgress&)>& onProgress = {},
                                      std::stop_token stop = {});

}
