#pragma once

#include "../BlenderJob.h"
#include "../EditSession.h"
#include "../Status.h"
#include "AssetSupports.h"
#include "CatalogNames.h"
#include "MaterialMaps.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// One asset on disk: Library/assets/items/<id>/ with
//   asset.blend   the cleaned model (see the conventions below)
//   asset.fbx     the same model exported by the worker for the app's live 3D preview, textures linked by
//                 relative paths into textures/; missing until the worker has written it
//   textures/     every image the model uses, linked from asset.blend by relative paths
//     maps/<key>/<channel>.<ext>   the image maps of each of the asset's materials (from milestone 10, owner
//                   decision): the same channels and conventions as a catalog material (MaterialMaps.h), made
//                   by the worker's bakers at cleanup; asset.blend's materials are rebuilt from these maps, so
//                   asset.blend, asset.fbx, the build and the export all carry only images. key is
//                   catalogIdFromName of the material name, made unique within the asset
//     preview/      copies of maps larger than kPreviewTextureSize on a side, scaled down to it; asset.fbx
//                   points at these instead, so the live preview loads small images
//   thumb.png     512 px render, missing when the render failed
//   info.json     this struct, in the format of schema/asset-info.schema.json
// Only core writes info.json. Blender reports what it did in its result file, and core writes info.json from
// that together with what the user chose (name, categories, tags, license).
//
// Conventions every cleaned asset follows (DESIGN.md section 7.12):
// - metres, Z up, the front faces -Y (Blender's front view), transforms applied on every object
// - the pivot is the bottom centre of the bounding box of all its objects, at the world origin
// - size.width is along X, size.depth along Y, size.height along Z
// - one collection named after the id holds every object; objects and meshes are named <id> when there is
//   one, otherwise <id>_01, <id>_02 and so on in order of their original names
// - cameras, lights and empties are removed; parented objects keep their place

inline constexpr std::string_view kAssetInfoFileName = "info.json";
inline constexpr std::string_view kAssetBlendFileName = "asset.blend";
inline constexpr std::string_view kAssetFbxFileName = "asset.fbx";
inline constexpr std::string_view kAssetThumbFileName = "thumb.png";
inline constexpr std::string_view kAssetTexturesFolder = "textures";
inline constexpr std::string_view kAssetInfoFormat = "plynth-asset";
inline constexpr int kAssetInfoVersion = 1;
inline constexpr std::string_view kAssetMapsFolder = "textures/maps";
inline constexpr std::string_view kAssetPreviewFolder = "textures/preview";

// The longest side of an image the app's 3D preview loads (owner, milestone 10). Bigger maps get a scaled copy
// for every FBX the app loads (asset.fbx, building.fbx); asset.blend, building.blend and the export keep the
// full size.
inline constexpr int kPreviewTextureSize = 1024;

// The longest id, name and tag.
inline constexpr std::size_t kMaxAssetIdLength = 64;
inline constexpr std::size_t kMaxAssetNameLength = 120;
inline constexpr std::size_t kMaxTagLength = 40;

// Source kinds. Each importer names its own. The two store kinds are used by materials too.
inline constexpr std::string_view kSourceLocalFile = "local_file";   // the user's own files
inline constexpr std::string_view kSourceBlenderKit = "blenderkit";  // catalog/BlenderKit.h
inline constexpr std::string_view kSourceFabZip = "fab_zip";         // catalog/FabZip.h

// The license every new asset gets until the user sets one.
inline constexpr std::string_view kUnknownLicenseId = "unknown";

struct AssetSize {
    double width = 0.0;
    double depth = 0.0;
    double height = 0.0;

    bool operator==(const AssetSize&) const = default;
};

// Says whether Blender has measured an asset: every side above zero. An unmeasured asset shows "not measured"
// instead of a size and cannot be placed.
bool isMeasured(const AssetSize& size);

struct AssetSource {
    std::string kind;      // kSourceLocalFile and so on
    std::string reference; // the original file name (no folder), or the address it came from
    SessionTime importedAt{};

    bool operator==(const AssetSource&) const = default;
};

struct AssetThumbnail {
    bool rendered = false;  // thumb.png exists and is current
    std::string error;      // why the last render failed; empty when it did not fail
    std::string engine;     // "eevee"; "cycles" only in files written before milestone 8
    SessionTime renderedAt{};

    bool operator==(const AssetThumbnail&) const = default;
};

// The maps of one material inside an asset. File paths are relative to the item folder. Base color, normal,
// roughness and metallic always have a file; opacity only for a material that is not fully opaque; occlusion
// only when the source had one. previewMaps holds the files asset.fbx uses: the same file as in maps when it
// is no larger than kPreviewTextureSize, otherwise its copy in textures/preview/.
struct AssetMaterial {
    std::string name;        // the material's name in asset.blend, one of AssetInfo::materials
    MaterialMaps maps;       // files under kAssetMapsFolder
    MaterialMaps previewMaps;

    bool operator==(const AssetMaterial&) const = default;
};

struct AssetInfo {
    std::string id;
    std::string name;
    std::vector<std::string> categories; // category ids; empty means uncategorized
    std::vector<std::string> tags;       // sorted, no repeats
    std::string license = std::string(kUnknownLicenseId);
    AssetSource source;
    AssetSize size;
    long long triangles = 0;
    int objects = 0;
    std::vector<std::string> materials;  // material names inside asset.blend
    std::vector<std::string> textures;   // paths relative to the item folder, for example "textures/wood.png"
    long long blendBytes = 0;            // size and write time of asset.blend when it was last measured, so a
    SessionTime blendModified{};         // changed file is noticed even when assets.db was lost
    AssetThumbnail thumbnail;
    std::string blenderVersion;          // the Blender that cleaned or last measured it
    std::vector<BlenderWarning> warnings; // from the last cleanup or measure
    // info.json "material_maps", one per entry of materials, in the same order. Missing in files written before
    // milestone 10 (it reads as empty); rescan then lists the asset in needsRefresh so it is cleaned once more.
    std::vector<AssetMaterial> materialMaps;
    // info.json "supports" (AssetSupports.h): the flat places set dressing may use. Empty (no value) when the
    // asset was not measured for them yet; rescan then lists it in needsRefresh so it is cleaned once more.
    std::optional<std::vector<AssetSupport>> supports;

    bool operator==(const AssetInfo&) const = default;
};

// True when every material of the asset has its maps (materialMaps matches materials by name and order).
bool hasMaterialMaps(const AssetInfo& info);

struct AssetInfoRead {
    Status status;
    AssetInfo info;
};

// Reads and checks info.json. CannotRead, or BadFile with a JSON pointer for invalid JSON, a wrong format,
// a missing or unknown field, an id that breaks checkAssetId or differs from the folder name (when folder
// is given), a bad tag, or a size that is negative or not finite, a material map outside textures/ or a
// required map without a file. UnsupportedVersion when newer.
AssetInfoRead readAssetInfo(const std::filesystem::path& file, std::string_view folderName = {});

// Writes info.json through a temporary file and a rename. CannotWrite.
Status writeAssetInfo(const std::filesystem::path& file, const AssetInfo& info);

// Ids of assets, categories and room types: lower case letters a to z, digits and "_", starting with a
// letter, at most kMaxAssetIdLength characters. InvalidValue saying which rule was broken.
Status checkCatalogId(std::string_view id);

// Tags: lower case letters, digits, "_" and "-", starting with a letter or digit, at most kMaxTagLength.
Status checkTag(std::string_view tag);

// Names: 1 to kMaxAssetNameLength characters (not bytes), not only spaces.
Status checkCatalogName(std::string_view name);

// An id made from a file name: lower case, every run of other characters becomes one "_", trimmed, cut to
// the length limit, "a_" in front when it starts with a digit, "asset" when nothing is left.
// "Red Chair (2).obj" stem "Red Chair (2)" gives "red_chair_2".
std::string catalogIdFromName(std::string_view text);

// The base id when taken returns false for it, otherwise base_2, base_3 and so on (cut so it stays within
// the length limit).
std::string uniqueCatalogId(const std::string& base, const std::function<bool(std::string_view)>& taken);

}
