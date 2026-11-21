#pragma once

#include "../BlenderJob.h"
#include "../Settings.h"
#include "AssetInfo.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The two asset tasks of the Blender worker. File formats the worker reads, each one file in the worker's
// importer registry, blender/plynth/importers/: "blend", "fbx", "obj", "gltf" (.gltf and .glb), and "zip" (unpacked first, then
// the model inside is read; with several models the order of preference is blend, fbx, glb, gltf, obj, and
// two of the same best kind give several_models).
inline constexpr std::string_view kFormatBlend = "blend";
inline constexpr std::string_view kFormatFbx = "fbx";
inline constexpr std::string_view kFormatObj = "obj";
inline constexpr std::string_view kFormatGltf = "gltf";
inline constexpr std::string_view kFormatZip = "zip";

// The thumbnail look (DESIGN.md section 7.11): three quarter view from the front right (camera 45 degrees
// round from -Y toward +X, 25 degrees up, 50 mm lens), framed so the bounding sphere fits with an 8 % margin,
// neutral white key, fill and rim area lights, a plain dark background in the theme's Ground color, the
// "Standard" view transform so that color is exact, square PNG, 8 bit RGB.
inline constexpr int kThumbnailSize = 512;
inline constexpr std::array<double, 3> kThumbnailBackground{0.0549, 0.0667, 0.0863}; // #0E1116, sRGB 0 to 1

struct ThumbnailSpec {
    bool render = true;
    int size = kThumbnailSize;
    ThumbnailEngine engine = ThumbnailEngine::Eevee;
    std::array<double, 3> background = kThumbnailBackground;
};

// cleanup_asset: read a model, clean it to the conventions in AssetInfo.h, save asset.blend and textures/
// into outputFolder, measure it, run the import checks and render thumb.png.
// Params: {"source": {"file": "D:/in/chair.obj", "format": "obj"}, "asset_id": "chair",
//          "output_folder": "D:/Library/assets/.staging/chair-3f9a07c1/item", "scale": 1.0, "rotate_z": 0,
//          "triangle_warning": 500000,
//          "thumbnail": {"render": true, "size": 512, "engine": "eevee", "background": [0.0549, 0.0667, 0.0863]}}
// Output: {"blend_file": "asset.blend", "picked_file": "chair/chair.fbx" or null (the model used from a zip),
//          "size": {"width": 0.45, "depth": 0.5, "height": 0.9}, "triangles": 1200, "objects": 2,
//          "materials": ["Wood"], "textures": ["textures/wood.png"], "missing_textures": ["oak.png"],
//          "thumbnail": {"file": "thumb.png", "engine": "eevee", "error": null}}
// A failed render does not fail the job: thumbnail.file is null and thumbnail.error says why.
struct CleanupJob {
    std::filesystem::path sourceFile;
    std::string format;
    std::string assetId;
    std::filesystem::path outputFolder;
    double scale = 1.0; // multiplies the size after the file's own units are applied
    int rotateZ = 0;    // degrees, 0, 90, 180 or 270, turned before the pivot is set
    long long triangleWarning = kDefaultTriangleWarning;
    ThumbnailSpec thumbnail;
};

// render_thumbnail: open an existing asset.blend without changing it, measure it when asked, and render
// thumb.png. Used for the retry button and when asset.blend changed on disk.
// Params: {"blend_file": "D:/Library/assets/items/chair/asset.blend",
//          "output_file": "D:/Library/assets/items/chair/thumb.png", "measure": true, "thumbnail": {...}}
// Output: {"size": {...}, "triangles": 1200, "objects": 2, "materials": [...], "textures": [...],
//          "thumbnail": {"file": "thumb.png", "engine": "cycles", "error": null}}
// size, triangles, objects, materials and textures are null when measure is false. A failed render fails
// the job with render_failed.
struct ThumbnailJob {
    std::filesystem::path blendFile;
    std::filesystem::path outputFile;
    bool measure = true;
    ThumbnailSpec thumbnail;
};

Json makeCleanupJob(const CleanupJob& job, std::string_view jobId, const std::filesystem::path& resultFile);
Json makeThumbnailJob(const ThumbnailJob& job, std::string_view jobId, const std::filesystem::path& resultFile);

// What the asset tasks report, read and checked. BadFile when a field is missing or a size is negative or
// not finite.
struct AssetJobOutput {
    BlenderResultHeader header;
    std::string blendFile;
    std::string pickedFile;
    bool measured = false;
    AssetSize size;
    long long triangles = 0;
    int objects = 0;
    std::vector<std::string> materials;
    std::vector<std::string> textures;
    std::vector<std::string> missingTextures;
    bool thumbnailRendered = false;
    std::string thumbnailEngine;
    std::string thumbnailError;
};

AssetJobOutput readCleanupResult(const Json& result, std::string_view jobId);
AssetJobOutput readThumbnailResult(const Json& result, std::string_view jobId);

}
