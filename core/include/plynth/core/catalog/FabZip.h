#pragma once

#include "../Status.h"
#include "../ZipListing.h"
#include "MaterialInfo.h"
#include "MaterialJobs.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Fab zip import. The user downloads zips from fab.com in the
// browser and picks one or more of them; Plynth never downloads from Fab. A Fab zip may
// hold one model in several formats, qualities and LODs, plus textures, often at several resolutions. Core
// reads the zip's file list (ZipListing.h), picks the preferred version and the textures that go with it, and
// writes a small source file for the worker. The worker unpacks only those entries, reads the model and
// builds each material from the textures by their file names.
//
// Registry entries, one file each in core/src/catalog/:
//   importers/FabZipImporter.cpp                asset importer kFabImporterId and the integration
//                                               kFabIntegrationId (Integrations.h)
//   material_importers/FabZipMaterialImporter.cpp   material importer kFabImporterId
// Both read .zip only.
//
// Asset importer:
// - score 20 when the zip holds a model of a readable format and looksLikeFabModelZip is true, or when it
//   holds only unreadable models or only inner zips (so the user gets the Fab sentence below), or when the
//   listing failed with badName (plan then gives the listing's status); 5 for any other zip with a readable
//   model (the local file importer's 10 wins, so the Fab importer is used for such a zip only when picked by
//   id, from the Sources window or with --importer fab_zip); 0 otherwise
// - plan: listZipEntries, classifyFabZip, chooseFabModel with the options; writes workFolder/kFabSourceFileName
//   (fabSourceJson, kind "model", unpack folder workFolder/kFabUnpackFolder); file that source file, format
//   kFormatFab, sourceKind kSourceFabZip, reference the zip's file name, defaultName catalogNameFromStem of the
//   picked model's item without its last word when that word is fabItemId, license as below, originalIsZip
//   true, details fabChoiceDetails
// License, in both importers: kUnknownLicenseId when source.guessed (a zip Plynth only guesses is from Fab,
// the saved license does not apply to it), otherwise the options' license.
// Material importer:
// - score 20 when the zip holds no readable model, holds texture images, and looksLikeFabMaterialZip is
//   true, or when the listing failed with badName; 5 for any other zip of images (texture_set's 10 wins);
//   0 otherwise
// - plan: as above with chooseFabMaterial, kind "material", workerSource kMaterialSourceFab, path the source
//   file, sourceKind kSourceFabZip, defaultName from the zip's stem without a last quality word and without
//   fabItemId as the word before it. No other word is dropped from a name.
// The integration: storeSourceKinds kSourceFabZip. optionFields lists formats (four choices, each a whole
// order with another format first and the rest in kFabFormats order), quality, max_texture_size (512 to
// 16384 and null for no limit), normal and license.
//
// Refusals (InvalidValue, plain sentences):
// - no readable model: "This zip holds no model Plynth can read (found: .uasset). Download the FBX or glTF
//   version from Fab." (the found endings listed, or "found: nothing")
// - only zips inside: "This zip holds other zip files. Unzip it and import the zip inside."
// - several different models: "This zip holds several models: Chair, Table, Lamp. Pick one with the option
//   model." (at most 10 shown names, then "and N more"); the details list every candidate so the app can
//   offer them
// - an option model that is not a readable model entry of the zip: "The zip has no model <entry>."
// - an unsafe or encrypted entry among those picked: "The zip holds <entry>, which cannot be unpacked safely."
// - a material zip without images: "This zip holds no texture images."

inline constexpr std::string_view kFabImporterId = "fab_zip";
inline constexpr std::string_view kFabIntegrationId = "fab";
inline constexpr std::string_view kFabSite = "https://www.fab.com/";

// The cleanup_asset format and the cleanup_material source kind that read a Fab source file
// (blender/plynth/importers/fab.py and blender/plynth/materials/sources/fab.py).
inline constexpr std::string_view kFormatFab = "fab";
inline constexpr std::string_view kMaterialSourceFab = "fab";

// The source file the importer writes into the import's work folder, schema/fab-source.schema.json, and the
// folder the worker unpacks into.
inline constexpr std::string_view kFabSourceFileName = "source.fab.json";
inline constexpr std::string_view kFabSourceFormat = "plynth-fab-source";
inline constexpr int kFabSourceVersion = 1;
inline constexpr std::string_view kFabUnpackFolder = "unpacked";

// Model quality words found in Fab paths.
enum class FabQuality { Low, Medium, High, Raw };

// "low", "medium", "high", "raw", and back. Empty for an unknown name.
std::string_view fabQualityName(FabQuality quality);
std::optional<FabQuality> fabQualityFromName(std::string_view name);

// Model formats the Fab importer reads, by ending: "fbx" (.fbx), "gltf" (.gltf, .glb), "obj" (.obj),
// "blend" (.blend). Other model endings are listed as unreadable: .uasset, .umap, .usd, .usda, .usdc, .usdz,
// .unitypackage, .max, .ma, .mb, .c4d, .3ds, .dae, .abc, .stl, .ply, .spp.
inline constexpr std::string_view kFabFormats[] = {"fbx", "gltf", "obj", "blend"};

// A texture to use for one channel of one material, picked by the user. material "*" means every material, a
// material's own pick wins over it. A named pick changes that material only.
struct FabMapPick {
    std::string material;
    MapChannel channel = MapChannel::BaseColor;
    std::string entry; // an image entry of the zip

    bool operator==(const FabMapPick&) const = default;
};

// The options of the Fab integration. Saved in integrations.json (Integrations.h):
//   "formats":          the readable formats in order of preference, at least one, no repeats;
//                       default ["fbx", "gltf", "obj", "blend"]
//   "quality":          default "medium"
//   "max_texture_size": the largest texture resolution to take when a zip has several, 256 to 16384, or null
//                       for the largest there is; default 4096
//   "normal":           "auto", "opengl" or "directx" (NormalConvention); default "auto"
//   "license":          the license id of zips the user imports as Fab by hand (a guessed zip gets unknown),
//                       at most 64 of a-z, 0-9 and "_" starting with a letter (as the schema); default
//                       "royalty_free"
// For one import only (refused when saving):
//   "model":            the zip entry of the model to take
//   "maps":             {"<material name or *>": {"<channel>": "<image entry>"}} (FabMapPick)
struct FabOptions {
    std::vector<std::string> formats{"fbx", "gltf", "obj", "blend"};
    FabQuality quality = FabQuality::Medium;
    std::optional<int> maxTextureSize = 4096;
    NormalConvention normal = NormalConvention::Auto;
    std::string license = "royalty_free";
    std::string model;
    std::vector<FabMapPick> maps;
};

// Reads the options. InvalidValue naming the option for an unknown key, a bad value, or (with forImport
// false) a per import key.
Status fabOptionsFromJson(const Json& json, bool forImport, FabOptions& out);

// The saved options as JSON, in the order above, without the per import keys.
Json fabOptionsToJson(const FabOptions& options);

// What each entry of a zip is. Words are the parts of each path, split on "/", "_", "-", ".", spaces and lower
// to upper case changes, compared in lower case.
// - quality words: low lowpoly lp -> Low; medium med mid -> Medium; high highpoly hp -> High;
//   raw nanite source original -> Raw
// - LOD: a word "lod" followed by digits, or "lod" then a word of digits
// - texture resolution: a word "<n>k" with n 0.5 (also "05k"), 1, 2, 4, 8 or 16 (n times 1024 pixels), or
//   one of 256, 512, 1024, 2048, 4096, 8192 standing alone
// - previews (never used as a texture): an image in a folder whose whole name is preview, thumb, thumbnail,
//   render, screenshot or cover, or the plural of one; or an image whose last word, after number and
//   resolution words are left out, is one of these words; or whose first word is preview, thumb, thumbnail
//   or screenshot. The same words elsewhere belong to the item's name (Manhole_Cover_BaseColor).
// - the stem of a model: the words of its file name without quality, LOD, resolution and format words, joined
//   with "_"; when nothing is left, the stem of its folder the same way
// - the item of a model, which names it: its stem. When the stem is only one of the general words model,
//   models, mesh, meshes, scene, scenes, object, objects, geometry, asset, export, exports, file, files or
//   untitled, the plain words of the nearest folder that has plain words other than these, joined with "_"
//   (Pack/Chair/Meshes/model.fbx is Chair). The stem when there is no such folder.
// - the folder words of a model: the plain words of all its entry's folders, from the top down, without the
//   general words and without the words of its item, ignoring case. Quality, LOD, resolution and format
//   words are not plain, so they never count.
// - two models are versions of one model when their items are equal and their folder words are equal, both
//   ignoring case. Otherwise they are different models: Red/Chair.fbx and Blue/Chair.fbx are two,
//   Models/Export/Chair.fbx and Meshes/Chair.fbx are one.
// - the shown name of a model: its item. When another model of the zip has that item and other folder words,
//   the folders that hold its folder words come first, each with only those words joined by "_" and followed
//   by "/": "Red/Chair", "Set/Blue/Chair".
struct FabModel {
    std::string entry;
    std::string format;                  // one of kFabFormats
    std::optional<FabQuality> quality;   // from the entry's path, nearest word to the file wins
    std::optional<int> lod;
    std::string stem;
    std::string item; // the model's name, which alone does not tell the models of a pack apart

    bool operator==(const FabModel&) const = default;
};

struct FabTexture {
    std::string entry;
    std::optional<int> resolution;       // pixels across, from the path's words

    bool operator==(const FabTexture&) const = default;
};

struct FabZipContents {
    std::vector<FabModel> models;            // readable models, sorted by entry
    std::vector<FabTexture> textures;        // images with a textureImageSuffixes ending, previews left out
    std::vector<std::string> previews;
    std::vector<std::string> unreadableModels; // entries with an unreadable model ending
    std::vector<std::string> innerZips;
    std::vector<std::string> metadata;       // .json entries
    std::vector<std::string> other;          // everything else that is not a folder, among them .bin and .mtl
    std::vector<std::string> unsafe;         // unsafe or encrypted entries, never used
};

// Sorts the entries of a zip into the lists above. Pure: it reads no file.
FabZipContents classifyFabZip(const std::vector<ZipEntry>& entries);

// True when the models look like versions from Fab: two or more readable models with one stem (different
// models too, so a pack gets the several models refusal), or a model with a LOD word or a quality word other
// than "source" and "original" (those two only count for picking), or a model in a folder named after a
// format (fbx, gltf, glb, obj, usd).
bool looksLikeFabModelZip(const FabZipContents& contents);

// False when the zip holds a material.json in any folder and any letter case: that is the user's own
// texture set, whose fields only texture_set keeps. Otherwise true when the textures come at two or more
// resolutions, or another .json is there.
bool looksLikeFabMaterialZip(const FabZipContents& contents);

// The Fab id of the zip's item, with evidence only: the file stem of a .json of the zip, 6 to 10 lower case
// letters and digits, that is also a whole word in the file name of one of its models or textures. The first
// by name when several are, empty when none is.
std::string fabItemId(const FabZipContents& contents);

// What was picked from a zip.
struct FabChoice {
    Status status;                      // the refusals above
    std::optional<FabModel> model;      // empty for a material
    std::vector<std::string> textures;  // the images taken, sorted
    // The largest resolution taken, when the textures came in several. Each map has its own, so some taken
    // may be smaller.
    std::optional<int> textureResolution;
    std::optional<std::string> metadata;  // a .json beside the model or the textures, for the worker to read
    std::vector<std::string> files;     // every entry the worker unpacks: the model, its companions, the
                                        // textures and the metadata, sorted, no repeats
    std::vector<std::pair<std::string, std::string>> skipped; // entry and a short reason, for the details
    // Shown names, one for each different model: with the several models refusal all of them, after a pick
    // those of the models not picked. Empty when every model is a version of one.
    std::vector<std::string> otherStems;
};

// Picks the model and its textures:
// 1. With the option model: that entry (it must be a readable model).
// 2. Otherwise: two or more different models (the versions rule above), whatever their formats, are the
//    "several models" refusal with their shown names.
//    Then the models of the first format in formats that the zip has; among them the preferred quality,
//    then the nearest other quality (medium: high, low, raw; high: raw, medium, low; low: medium, high, raw;
//    raw: high, medium, low), models without a quality word after all of those; then the lowest LOD, models
//    without a LOD after LOD 0; then the shortest entry; then the first by name.
// 3. Companions: .bin and .mtl files in the model's folder.
// 4. Textures: going up from the model's folder, the first folder whose part of the zip holds textures gives
//    them, leaving out folders named for another quality or another format than the model's. Then, for each
//    texture on its own, where one texture is every entry with the same path once resolution words, parts
//    made only of those, and the ending are left out, ignoring case and separators: its largest resolution
//    up to maxTextureSize (its largest when that is null, its smallest when all are larger). So a map that
//    exists only at another resolution is still taken. Textures without a resolution word are always kept.
//    Each texture named in the option maps is added when it is not already taken.
//    A refusal of an unsafe entry also covers an ending hidden behind a stream name or trailing dots and
//    spaces ("a.png:stream", "a.png."), and an unsafe entry named by the options model and maps.
// 5. Metadata: a .json in the model's folder or the folder the textures came from, the first by name.
FabChoice chooseFabModel(const FabZipContents& contents, const FabOptions& options);

// Picks the textures of a material zip: every texture, filtered by resolution as in step 4, plus the maps
// option's images; metadata the first .json by name. No model.
FabChoice chooseFabMaterial(const FabZipContents& contents, const FabOptions& options);

// The worker's source file, schema/fab-source.schema.json:
//   {"format": "plynth-fab-source", "version": 1, "zip": "D:/Downloads/Chair.zip", "kind": "model",
//    "unpack_folder": "D:/Library/assets/.staging/chair-3f9a07c1/unpacked",
//    "files": [...], "model": "Chair/FBX/Medium/Chair_LOD0.fbx", "model_format": "fbx",
//    "textures": [...], "metadata": "Chair/Chair.json" or null,
//    "maps": {"*": {"base_color": "Chair/Textures/T_Chair_BaseColor_2K.png"}}, "normal": "auto"}
// kind is "model" or "material"; model and model_format are null for a material.
Json fabSourceJson(const FabChoice& choice, std::string_view kind, const std::filesystem::path& zip,
                   const std::filesystem::path& unpackFolder, const FabOptions& options);

// ImportPlan::details for a Fab zip:
//   {"models": [{"entry", "format", "quality" or null, "lod" or null, "stem", "item"}...], "picked": entry or null,
//    "textures": [...], "texture_resolution": 2048 or null, "metadata": entry or null,
//    "skipped": [{"entry", "reason"}...], "other_models": [shown names...], "unreadable": [...], "previews": [...]}
Json fabChoiceDetails(const FabZipContents& contents, const FabChoice& choice);

}
