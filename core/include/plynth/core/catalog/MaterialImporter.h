#pragma once

#include "../Status.h"
#include "Importer.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Material importers are registry entries, separate from the asset importers so the two catalogs share
// nothing. Each lives in its own file in core/src/catalog/material_importers/ with a MaterialImporterRegistrar
// at namespace scope, built into the OBJECT library plynth_importers, so adding one never means editing
// existing code. Fab zips and BlenderKit addresses (FabZip.h and BlenderKit.h) are two more entries.
//
// An importer only decides what the Blender worker reads and with which of the worker's material sources
// (blender/plynth/materials/sources/). Matching image files to channels by their names, unpacking zips and
// reading material.json are done by the worker. Core may read a zip's list of files (ZipListing.h) but never
// unpacks one.

// Worker material source ids (schema/blender-job.schema.json "cleanup_material").
inline constexpr std::string_view kMaterialSourceTextureSet = "texture_set";
inline constexpr std::string_view kMaterialSourceBlend = "blend";

// Image endings a texture set may hold, lower case with the dot.
// .png .jpg .jpeg .tga .tif .tiff .bmp .exr
std::span<const std::string_view> textureImageSuffixes();

// What an importer decided.
struct MaterialImportPlan {
    std::string workerSource;   // kMaterialSourceTextureSet, kMaterialSourceBlend or kMaterialSourceFab
    std::filesystem::path path; // the folder, zip, image file, .blend or Fab source file the worker reads
    std::string sourceKind;     // for info.json, for example kSourceTextureSet
    std::string reference;      // for info.json: the original file or folder name, or the address
    std::string defaultName;    // used when neither the request nor material.json names it
    std::string license;        // used when neither names one; empty means kUnknownLicenseId
    bool originalIsZip = false; // the original is removed after the import unless kept, as for assets
    // As in ImportPlan: a Blender job that fetches the source first (path and workerSource are then filled
    // from it, and defaultName, license and reference where still empty), and what the importer chose.
    std::optional<ImportFetch> fetch;
    Json details = Json::object();
    // The material to take from a .blend when the importer knows it (a fetched BlenderKit material). The
    // request's blendMaterial wins over it. Empty for the only one.
    std::string blendMaterial;
};

class MaterialImporter {
public:
    virtual ~MaterialImporter() = default;

    // Stable lower case id, for example "texture_set".
    virtual std::string_view id() const = 0;
    // Short name for the import screen, for example "Texture set".
    virtual std::string_view name() const = 0;
    // One plain sentence on what it takes.
    virtual std::string_view description() const = 0;
    // File endings it reads, for file dialogs. Empty for one that takes addresses.
    virtual std::vector<std::string> fileSuffixes() const = 0;
    // How well it handles a source: 0 not at all, higher wins.
    virtual int score(const ImportSource& source) const = 0;
    // Decides what the worker reads. InvalidValue with a plain sentence when the source cannot be used;
    // out.details may still say what it found.
    virtual Status plan(const ImportSource& source, const std::filesystem::path& workFolder,
                        MaterialImportPlan& out) const = 0;
};

// Adds an importer. False, and it is dropped, when its id is taken or not a well formed lower case name.
bool registerMaterialImporter(std::unique_ptr<MaterialImporter> importer);

// Registers a material importer while the program starts.
struct MaterialImporterRegistrar {
    explicit MaterialImporterRegistrar(std::unique_ptr<MaterialImporter> importer);
};

// The importer with this id. Null when there is none.
const MaterialImporter* findMaterialImporter(std::string_view id);

// Every material importer, sorted by id.
std::vector<const MaterialImporter*> allMaterialImporters();

// The importer with the highest score; ties go to the lower id. Null when every score is 0.
const MaterialImporter* pickMaterialImporter(const ImportSource& source);

// Every file ending some material importer reads, sorted, without repeats.
std::vector<std::string> importableMaterialSuffixes();

// The texture set importer (id "texture_set", name "Texture set"), core/src/catalog/material_importers/:
// - a folder scores 10 when it holds an image with a textureImageSuffixes ending directly in it or one folder
//   down, or a material.json with a "values" object or a "base_color_value" (no image needed); a single image file and a .zip score 10
// - worker source texture_set, source kind kSourceTextureSet, reference the folder or file name, default name
//   from the folder or file stem (catalogNameFromStem), no license, originalIsZip for .zip
inline constexpr std::string_view kTextureSetImporterId = "texture_set";

// The Blender material importer (id "blend_material", name "Material from a .blend file"):
// - a .blend scores 10; worker source blend, source kind kSourceBlendMaterial, reference the file name,
//   default name the picked material's name (the worker reports it), no license
// - used for a material inside an asset's asset.blend too ("Add to materials catalog")
inline constexpr std::string_view kBlendMaterialImporterId = "blend_material";

}
