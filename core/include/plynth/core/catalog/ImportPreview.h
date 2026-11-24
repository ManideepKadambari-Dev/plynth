#pragma once

#include "../Library.h"
#include "../Status.h"
#include "Importer.h"
#include "MaterialImport.h"
#include "MaterialImporter.h"

#include <nlohmann/json.hpp>

#include <string>

namespace plynth {

using Json = nlohmann::ordered_json;

// Seeing what an import would do without doing it: which importer takes the source, with
// which options, and what its plan picked. Runs no Blender and changes nothing in the library: the importer's
// plan runs in a work folder under the catalog's .staging/ that is deleted afterwards. Used by
// "assets import --dry-run", "materials import --dry-run" and the app's Fab import check.
//
// importAsset and importMaterial start with the same two steps: the importer is
// request.importer when set, otherwise the picked one; ImportSource::options comes from optionsForImporter
// and ImportSource::guessed is set the same way for both, so a dry run shows the license the import records.

struct ImportPreview {
    Status status;           // as the import's first steps would answer: NotFound, InvalidValue, BadFile
    std::string importer;    // the importer's id
    std::string integration; // its integration's id, or empty
    Json options = Json::object(); // what plan was given
    // As below. When the importer refuses: {"details": {...}} if its plan gave details (for example the
    // several models of a Fab zip), otherwise an empty object.
    Json plan = Json::object();
};

// Previews an asset import. plan is
//   {"file": "..." or null, "format": "fab" or null, "source_kind": "fab_zip", "reference": "Chair.zip",
//    "default_name": "Chair", "license": "royalty_free" or null, "original_is_zip": true,
//    "fetch": null or {"task": "blenderkit_fetch", "kind": "model", "params": {...}},
//    "details": {...}}
// file is null when a fetch brings it. A Fab source file the plan wrote is described by details, not kept.
ImportPreview previewAssetImport(const LibraryPaths& library, const ImportRequest& request);

// Previews a material import, plan as above with "worker_source" in place of "format" and "blend_material".
ImportPreview previewMaterialImport(const LibraryPaths& library, const MaterialImportRequest& request);

// The JSON of a preview, the result of the --dry-run commands:
//   {"importer": "fab_zip", "integration": "fab", "options": {...}, "plan": {...}}
Json importPreviewToJson(const ImportPreview& preview);

}
