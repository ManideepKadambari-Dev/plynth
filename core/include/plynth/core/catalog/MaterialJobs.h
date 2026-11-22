#pragma once

#include "../BlenderJob.h"
#include "AssetJobs.h"
#include "MaterialInfo.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The material task of the Blender worker, kCleanupMaterialTask (DESIGN.md sections 7.11 and 7.12). It reads a
// source with one of the worker's material sources, makes every map by the conventions of MaterialInfo.h into
// outputFolder/textures/, runs the material checks and renders thumb.png into outputFolder. Used for imports
// and for refreshMaterial (then the source is the item folder itself, with its maps named in maps).
//
// Blender's own texture baking needs Cycles, which Plynth never uses (owner, milestone 7). So the worker makes
// maps without it, channel by channel, with its baker registry (blender/plynth/materials/bakers/):
//   "direct": a channel that is an image or a single value in the source (an image texture linked straight to
//             the Principled input, through a Normal Map node for normals, or an unlinked input) is copied or
//             written as a constant image
//   "eevee_render": any other channel of a .blend material is rendered with Eevee: a flat tile one tileSize
//             across, seen straight down by an orthographic camera at bakeSize pixels, with the channel's node
//             output routed to an emission shader and the Raw view transform (Standard for base color)
//
// Params, schema/blender-job.schema.json "cleanup_material":
//   {"source": {"kind": "texture_set", "path": "D:/in/oak", "material": null,
//               "maps": {"base_color": "D:/in/oak/oak_col.jpg"}, "values": {"metallic": 0.0},
//               "normal": "auto"},
//    "output_folder": "D:/Library/materials/.staging/material-3f9a07c1/item",
//    "tile_size": {"width": 2.0, "height": 2.0} or null, "bake_size": 1024,
//    "thumbnail": {"render": true, "size": 512, "background": [0.0549, 0.0667, 0.0863]}}
//   kind "texture_set": path is a folder, a zip (unpacked by the worker) or one image file; images are matched
//     to channels by their names (the worker's name table), material.json next to them is read, and maps and
//     values given here win over both.
//   kind "blend": path is a .blend; material names the material, or null for the only one (several give
//     several_materials, listing them).
//   normal: "auto" (a name with gl or opengl is OpenGL, dx or directx is DirectX, otherwise OpenGL), "opengl"
//     or "directx"; DirectX maps get their green channel flipped.
//   tile_size null: material.json's, otherwise kDefaultTileSize.
// Output:
//   {"maps": {"base_color": {"file": "textures/base_color.jpg", "from": "file", "detail": "oak_col.jpg"},
//             "normal": {...}, "roughness": {...}, "metallic": {...},
//             "opacity": {"file": null, "from": "none", "detail": ""}, "occlusion": {...}},
//    "texture_size": {"width": 2048, "height": 2048}, "tile_size": {"width": 2.0, "height": 2.0},
//    "picked_material": "Wood" or null,
//    "source_info": null or {"id": "oak_planks", "name": "Oak planks", "groups": ["floor"], "tags": ["wood"],
//      "license": "own_work"} (what material.json said; each field may be missing),
//    "thumbnail": {"file": "thumb.png", "engine": "eevee", "error": null}}
// A failed render does not fail the job: thumbnail.file is null and thumbnail.error says why.
// Errors: import_failed (no image or nothing readable), no_base_color, several_materials, bake_failed (an Eevee
// render of a channel failed), unpack_failed, save_failed. Warnings: ambiguous_map (several images fit one
// channel; the largest was used and the message names it), unused_image (an image matched no channel),
// map_size_mismatch, visible_seams (the opposite edges of a baked or given map differ, so it may not tile),
// low_resolution (fewer than kLowResolutionPixelsPerMetre base color pixels per metre).

inline constexpr int kDefaultBakeSize = 1024;
inline constexpr int kMinBakeSize = 256;
inline constexpr int kMaxBakeSize = 4096;
inline constexpr int kLowResolutionPixelsPerMetre = 256;

// The material thumbnail (DESIGN.md section 7.11): a sphere 1 m across on the left and a flat tile 1 m square
// on the right, tilted 30 degrees toward the camera, both with UVs of metres divided by the tile size so the
// material shows at real world size; the asset thumbnail's lights, background and view transform; square PNG,
// kThumbnailSize, rendered with Eevee only.

enum class NormalConvention { Auto, OpenGl, DirectX };

// "auto", "opengl" or "directx", and back. Empty for an unknown name.
std::string_view normalConventionName(NormalConvention convention);
std::optional<NormalConvention> normalConventionFromName(std::string_view name);

struct MaterialCleanupJob {
    std::string workerSource;            // kMaterialSourceTextureSet or kMaterialSourceBlend
    std::filesystem::path path;
    std::string blendMaterial;           // blend only; empty for the only one
    std::vector<std::pair<MapChannel, std::filesystem::path>> maps; // files the user picked per channel
    // Single values from 0 to 1 for roughness, metallic, opacity or occlusion; importMaterial refuses any other
    // channel with InvalidValue.
    std::vector<std::pair<MapChannel, double>> values;
    NormalConvention normal = NormalConvention::Auto;
    std::filesystem::path outputFolder;  // an empty folder
    std::optional<TileSize> tileSize;
    int bakeSize = kDefaultBakeSize;     // a power of two from kMinBakeSize to kMaxBakeSize
    ThumbnailSpec thumbnail;
};

Json makeMaterialCleanupJob(const MaterialCleanupJob& job, std::string_view jobId,
                            const std::filesystem::path& resultFile);

// What material.json said, as the worker reported it.
struct MaterialSourceInfo {
    std::optional<std::string> id;
    std::optional<std::string> name;
    std::optional<std::vector<std::string>> groups;
    std::optional<std::vector<std::string>> tags;
    std::optional<std::string> license;

    bool operator==(const MaterialSourceInfo&) const = default;
};

// The task's output, read and checked. BadFile when a field is missing, a required map has no file, a file is
// outside textures/, or a size is not finite or out of range.
struct MaterialJobOutput {
    BlenderResultHeader header;
    MaterialMaps maps;
    TextureSize textureSize;
    TileSize tileSize;
    std::string pickedMaterial;
    std::optional<MaterialSourceInfo> sourceInfo;
    bool thumbnailRendered = false;
    std::string thumbnailEngine;
    std::string thumbnailError;
};

MaterialJobOutput readMaterialCleanupResult(const Json& result, std::string_view jobId);

}
