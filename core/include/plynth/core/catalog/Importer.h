#pragma once

#include "../Status.h"
#include "AssetInfo.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Importers are registry entries (DESIGN.md sections 4 and 7.12). Each is one route assets come in by: own
// files now, BlenderKit and Fab zips in milestone 13. An importer lives in its own file in
// core/src/catalog/importers/ with an ImporterRegistrar at namespace scope, built into the OBJECT library
// plynth_importers like the detectors, so adding one never means editing existing code.
//
// An importer only decides what Blender should read. The shared steps (staging, the Blender cleanup job,
// info.json, the index, removing the original) are done once for every route by importAsset in AssetImport.h.

// What the user handed in: a file or folder on disk, or a web address. Exactly one is set.
struct ImportSource {
    std::filesystem::path file;
    std::string url;
};

// What the user asked for. Empty fields take defaults: the id and name from the file name, no categories
// (uncategorized), no tags, the importer's license or kUnknownLicenseId.
struct ImportRequest {
    ImportSource source;
    std::string id;
    std::string name;
    std::vector<std::string> categories;
    std::vector<std::string> tags;
    std::string license;
    double scale = 1.0;
    int rotateZ = 0;
    std::optional<bool> keepOriginal; // empty: the keepImportedZips setting
};

// What an importer decided.
struct ImportPlan {
    std::filesystem::path file;  // the file Blender reads (may be inside the work folder)
    std::string format;          // a format id from AssetJobs.h
    std::string sourceKind;      // for info.json, for example kSourceLocalFile
    std::string reference;       // for info.json: the original file name or the address
    std::string defaultName;     // used when the request has no name
    std::string license;         // used when the request has none; empty means kUnknownLicenseId
    // True when the original is a zip file that DESIGN.md section 5 rule 4 says to remove after a
    // successful import (moveToRecycleBin), unless the request or the settings keep it.
    bool originalIsZip = false;
};

class Importer {
public:
    virtual ~Importer() = default;

    // Stable lower case id, for example "local_file".
    virtual std::string_view id() const = 0;
    // Short name for the app's import screen, for example "Own files".
    virtual std::string_view name() const = 0;
    // One plain sentence on what it takes.
    virtual std::string_view description() const = 0;
    // File endings it reads, lower case with the dot, for file dialogs, for example ".obj". Empty for an
    // importer that takes addresses.
    virtual std::vector<std::string> fileSuffixes() const = 0;
    // How well it handles a source: 0 means not at all, higher wins. The local file importer gives 10 for its
    // endings, so a later importer that recognises a particular kind of zip can win by giving more.
    virtual int score(const ImportSource& source) const = 0;
    // Decides what Blender reads. workFolder is an empty folder of this import, for files it needs to make.
    // InvalidValue with a plain sentence when the source cannot be used after all.
    virtual Status plan(const ImportSource& source, const std::filesystem::path& workFolder,
                        ImportPlan& out) const = 0;
};

// Adds an importer. Returns false and drops it when its id is taken or not a well formed lower case name.
bool registerImporter(std::unique_ptr<Importer> importer);

// Registers an importer while the program starts, for example
// const ImporterRegistrar registrar(std::make_unique<LocalFileImporter>());
struct ImporterRegistrar {
    explicit ImporterRegistrar(std::unique_ptr<Importer> importer);
};

// The importer with this id. Null when there is none.
const Importer* findImporter(std::string_view id);

// Every importer, sorted by id.
std::vector<const Importer*> allImporters();

// The importer with the highest score for a source; ties go to the lower id. Null when every score is 0.
const Importer* pickImporter(const ImportSource& source);

// Every file ending some importer reads, sorted, without repeats.
std::vector<std::string> importableSuffixes();

// The local file importer (id "local_file", name "Own files"), core/src/catalog/importers/LocalFileImporter.cpp:
// - endings .blend, .fbx, .obj, .gltf, .glb and .zip, any letter case, score 10
// - a folder scores 10 when it holds a model file (.blend, .fbx, .obj, .gltf, .glb) directly in it or one
//   folder down; the plan picks one with the zip preference of AssetJobs.h and Blender reads it in place;
//   two of the best kind is InvalidValue naming them
// - format from the ending (.glb is "gltf"), source kind kSourceLocalFile, reference the file or folder name,
//   default name from the stem (catalogNameFromStem), no license, originalIsZip for .zip
// - an .obj next to its .mtl and textures is read in place, so its relative texture paths work
inline constexpr std::string_view kLocalFileImporterId = "local_file";

}
