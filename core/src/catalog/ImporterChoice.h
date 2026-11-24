#pragma once

#include "plynth/core/Library.h"
#include "plynth/core/Status.h"
#include "plynth/core/catalog/Importer.h"
#include "plynth/core/catalog/Integrations.h"
#include "plynth/core/catalog/MaterialImport.h"
#include "plynth/core/catalog/MaterialImporter.h"

#include <string>

namespace plynth {

// Picking the importer and its options for an import: the first steps of importAsset and importMaterial and of
// the dry run. importer stays null with an ok status when no importer reads the source, so the caller words
// that refusal itself. source is the request's source with the options filled in.
template <class ImporterType>
struct ChosenImporter {
    Status status;
    const ImporterType* importer = nullptr;
    ImportSource source;
};

namespace importer_choice {

// The refusal for an importer that was named but does not read the source.
inline std::string cannotRead(std::string_view name)
{
    return std::string(name) + " cannot read this.";
}

// Takes the source's options from the integration of the importer.
template <class ImporterType>
void fillOptions(const LibraryPaths& library, const ImporterType& importer, bool material, const Json& requested,
                 ChosenImporter<ImporterType>& out)
{
    const ImporterOptions options = optionsForImporter(library, importer.id(), material, requested);
    if (!options.status.ok()) {
        out.status = options.status;
        return;
    }
    out.importer = &importer;
    out.source.options = options.options;
}

// Says whether Plynth guesses the source's origin: no importer is named and no license is given for this import.
inline bool isGuessed(const std::string& importer, const Json& requested)
{
    return importer.empty() && !(requested.is_object() && requested.contains("license"));
}

}

// The asset importer for a request: the one named by request.importer, else the best scoring.
inline ChosenImporter<Importer> chooseAssetImporter(const LibraryPaths& library, const ImportRequest& request)
{
    ChosenImporter<Importer> out;
    out.source = request.source;
    out.source.options = Json::object();
    out.source.guessed = importer_choice::isGuessed(request.importer, request.importerOptions);
    const Importer* importer = nullptr;
    if (!request.importer.empty()) {
        importer = findImporter(request.importer);
        if (!importer) {
            out.status = fail(ErrorCode::NotFound, "There is no importer called " + request.importer + ".");
            return out;
        }
        if (importer->score(out.source) == 0) {
            out.status = fail(ErrorCode::InvalidValue, importer_choice::cannotRead(importer->name()));
            return out;
        }
    } else {
        importer = pickImporter(out.source);
    }
    if (importer) importer_choice::fillOptions(library, *importer, false, request.importerOptions, out);
    return out;
}

// The same for a material request.
inline ChosenImporter<MaterialImporter> chooseMaterialImporter(const LibraryPaths& library,
                                                               const MaterialImportRequest& request)
{
    ChosenImporter<MaterialImporter> out;
    out.source = request.source;
    out.source.options = Json::object();
    out.source.guessed = importer_choice::isGuessed(request.importer, request.importerOptions);
    const MaterialImporter* importer = nullptr;
    if (!request.importer.empty()) {
        importer = findMaterialImporter(request.importer);
        if (!importer) {
            out.status = fail(ErrorCode::NotFound, "There is no importer called " + request.importer + ".");
            return out;
        }
        if (importer->score(out.source) == 0) {
            out.status = fail(ErrorCode::InvalidValue, importer_choice::cannotRead(importer->name()));
            return out;
        }
    } else {
        importer = pickMaterialImporter(out.source);
    }
    if (importer) importer_choice::fillOptions(library, *importer, true, request.importerOptions, out);
    return out;
}

}
