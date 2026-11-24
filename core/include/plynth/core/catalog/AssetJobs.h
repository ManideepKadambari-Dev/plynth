#pragma once

#include "../BlenderJob.h"
#include "../Settings.h"
#include "AssetInfo.h"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The two asset tasks of the Blender worker. File formats the worker reads, each one file in the worker's
// importer registry, blender/plynth/importers/: "blend", "fbx", "obj", "gltf" (.gltf and .glb), "zip" (unpacked first, then
// the model inside is read; with several models the order of preference is blend, fbx, glb, gltf, obj, and
// two of the same best kind give several_models), and "fab" (kFormatFab in FabZip.h: the file is a Fab source
// file, schema/fab-source.schema.json, naming the zip, the one model to read and the entries to unpack; the
// materials are built from the textures by file name when the model is read, so the step material_maps below
// treats them like any other). A store model that was fetched (StoreFetch.h) is a "blend".
inline constexpr std::string_view kFormatBlend = "blend";
inline constexpr std::string_view kFormatFbx = "fbx";
inline constexpr std::string_view kFormatObj = "obj";
inline constexpr std::string_view kFormatGltf = "gltf";
inline constexpr std::string_view kFormatZip = "zip";

// The thumbnail look: three quarter view from the front right (camera 45 degrees
// round from -Y toward +X, 25 degrees up, 50 mm lens), framed so the bounding sphere fits with an 8 % margin,
// neutral white key, fill and rim area lights, a plain dark background in the theme's Ground color, the
// "Standard" view transform so that color is exact, square PNG, 8 bit RGB, rendered with Eevee.
inline constexpr int kThumbnailSize = 512;
inline constexpr std::array<double, 3> kThumbnailBackground{0.0549, 0.0667, 0.0863}; // #0E1116, sRGB 0 to 1

struct ThumbnailSpec {
    bool render = true;
    int size = kThumbnailSize;
    std::array<double, 3> background = kThumbnailBackground;
};

// cleanup_asset: read a model, clean it to the conventions in AssetInfo.h, save asset.blend and textures/
// into outputFolder, export asset.fbx beside it, measure it, run the import checks and render thumb.png.
// Params: {"source": {"file": "D:/in/chair.obj", "format": "obj"}, "asset_id": "chair",
//          "output_folder": "D:/Library/assets/.staging/chair-3f9a07c1/item", "scale": 1.0, "rotate_z": 0,
//          "triangle_warning": 500000,
//          "thumbnail": {"render": true, "size": 512, "background": [0.0549, 0.0667, 0.0863]}}
// Output: {"blend_file": "asset.blend", "fbx_file": "asset.fbx" or null,
//          "picked_file": "chair/chair.fbx" or null (the model used from a zip or a Fab zip),
//          "size": {"width": 0.45, "depth": 0.5, "height": 0.9}, "triangles": 1200, "objects": 2,
//          "materials": ["Wood"], "textures": ["textures/wood.png"], "missing_textures": ["oak.png"],
//          "thumbnail": {"file": "thumb.png", "engine": "eevee", "error": null}}
//          "bake_size": 1024,
//          "material_maps": [{"name": "Wood",
//                             "maps": {"base_color": {"file": "textures/maps/wood/base_color.png",
//                                                     "from": "file", "detail": "wood.png"}, ...},
//                             "preview_maps": {"base_color": {"file": "textures/preview/wood_base_color.png",
//                                                             "from": "file", "detail": "wood.png"}, ...}}]
// (bake_size is a param; material_maps is in the output, one per entry of materials, in the same order.)
// Material maps: before asset.blend is saved, the step "material_maps" makes the maps of
// every material with the material bakers of catalog/MaterialJobs.h ("direct", then "eevee_render" over the
// material's own UV square, one tile 1 m across, at bake_size), writes them under kAssetMapsFolder, rebuilds
// the material from them, and writes a scaled copy under kAssetPreviewFolder of any map larger than
// kPreviewTextureSize, which asset.fbx uses. A material whose nodes use object or generated coordinates looks
// different once baked over its UV square (warning material_approximated); one that cannot be baked at all
// keeps a constant image of its base color value (warning material_approximated too). Never fails the job.
// A failed render does not fail the job: thumbnail.file is null and thumbnail.error says why. A failed
// asset.fbx does not fail it either: fbx_file is null with the warning fbx_failed.
// Thumbnails are rendered with Eevee only. There is no engine choice and no other
// engine to fall back to: when Eevee cannot render, the render has failed.
struct CleanupJob {
    std::filesystem::path sourceFile;
    std::string format;
    std::string assetId;
    std::filesystem::path outputFolder;
    double scale = 1.0; // multiplies the size after the file's own units are applied
    int rotateZ = 0;    // degrees, 0, 90, 180 or 270, turned before the pivot is set
    long long triangleWarning = kDefaultTriangleWarning;
    ThumbnailSpec thumbnail;
    int bakeSize = 1024; // pixels across a baked material map; the same limits as kDefaultBakeSize in MaterialJobs.h
};

// render_thumbnail: open an existing asset.blend without changing it, measure it when asked, render
// thumb.png, and export asset.fbx when fbx_file is given. Used for the retry button, when asset.blend changed
// on disk, and for assets that have no asset.fbx yet.
// Params: {"blend_file": "D:/Library/assets/items/chair/asset.blend",
//          "output_file": "D:/Library/assets/items/chair/thumb.png", "measure": true,
//          "fbx_file": "D:/Library/assets/items/chair/asset.fbx" or null, "thumbnail": {...}}
// Output: {"size": {...}, "triangles": 1200, "objects": 2, "materials": [...], "textures": [...],
//          "fbx_file": "asset.fbx" or null, "thumbnail": {"file": "thumb.png", "engine": "eevee", "error": null}}
// size, triangles, objects, materials and textures are null when measure is false. fbx_file in the output
// is the file name written, null when none was asked for or the export failed (warning fbx_failed). A failed
// render fails the job with render_failed.
struct ThumbnailJob {
    std::filesystem::path blendFile;
    std::filesystem::path outputFile;
    bool measure = true;
    std::filesystem::path fbxFile; // empty: no asset.fbx is written
    ThumbnailSpec thumbnail;
};

Json makeCleanupJob(const CleanupJob& job, std::string_view jobId, const std::filesystem::path& resultFile);
Json makeThumbnailJob(const ThumbnailJob& job, std::string_view jobId, const std::filesystem::path& resultFile);

// What the asset tasks report, read and checked. BadFile when a field is missing or a size is negative or
// not finite.
struct AssetJobOutput {
    BlenderResultHeader header;
    std::string blendFile;
    std::string fbxFile; // "asset.fbx", or empty when none was written
    std::string pickedFile;
    bool measured = false;
    AssetSize size;
    long long triangles = 0;
    int objects = 0;
    std::vector<std::string> materials;
    std::vector<std::string> textures;
    std::vector<std::string> missingTextures;
    std::vector<AssetMaterial> materialMaps; // cleanup only; BadFile when a required map has no file or a file
                                             // is outside textures/
    bool thumbnailRendered = false;
    std::string thumbnailEngine;
    std::string thumbnailError;
};

AssetJobOutput readCleanupResult(const Json& result, std::string_view jobId);
AssetJobOutput readThumbnailResult(const Json& result, std::string_view jobId);

}
