#pragma once

#include "../BlenderJob.h"
#include "../EditSession.h"
#include "../Status.h"
#include "AssetInfo.h"
#include "MaterialMaps.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// One material on disk: Library/materials/items/<id>/ with
//   textures/<channel>.<ext>        one image per map channel below (base_color.jpg, normal.png and so on),
//                                   made by the Blender worker; the export names copies after the material
//   thumb.png                       512 px render of a sphere and a flat tile side by side, missing when the
//                                   render failed
//   info.json                       this struct, in the format of schema/material-info.schema.json
// Only core writes info.json. The files in items/ are the truth; materials.db is an index that rescan rebuilds.
//
// Conventions every cleaned material follows (DESIGN.md sections 7.11 and 7.16, owner decision in milestone 8):
// - every channel the 3D preview and Unreal read is an image, never only a number: base color, normal,
//   roughness and metallic always have a file; opacity has one only when the material is not fully opaque,
//   occlusion only when the source had one
// - base color is sRGB; normal, roughness, metallic, opacity and occlusion are linear data; roughness,
//   metallic, opacity and occlusion are grey (the red channel is read)
// - normal maps are in the OpenGL convention (green points up, as in Blender)
// - one image covers one tile; tileSize is the real world size of one tile in metres (width along U,
//   height along V), so the build sets UVs to metres divided by tileSize
// - a channel that was a single value in the source is a small constant image (kConstantMapSize pixels)
// - png and jpg sources are copied as they are when no pixel had to change; anything else is written as png

inline constexpr std::string_view kMaterialInfoFileName = "info.json";
inline constexpr std::string_view kMaterialThumbFileName = "thumb.png";
inline constexpr std::string_view kMaterialTexturesFolder = "textures";
inline constexpr std::string_view kMaterialInfoFormat = "plynth-material";
inline constexpr int kMaterialInfoVersion = 1;

// The optional file next to a texture set that says what it is (schema/material-source.schema.json). The
// sample materials have one.
inline constexpr std::string_view kMaterialSourceFileName = "material.json";

inline constexpr int kConstantMapSize = 4;

// Tile sizes, metres.
inline constexpr double kDefaultTileSize = 1.0;
inline constexpr double kMinTileSize = 0.01;
inline constexpr double kMaxTileSize = 100.0;

// Source kinds for info.json. Each material importer names its own.
inline constexpr std::string_view kSourceTextureSet = "texture_set"; // a folder, zip or image files
inline constexpr std::string_view kSourceBlendMaterial = "blend_material"; // a material inside a .blend
// Store materials use kSourceBlenderKit and kSourceFabZip of AssetInfo.h.

// MapChannel, MapOrigin, MaterialMap and MaterialMaps are in MaterialMaps.h (shared with assets); they are
// still implemented in MaterialInfo.cpp.

struct TileSize {
    double width = kDefaultTileSize;
    double height = kDefaultTileSize;

    bool operator==(const TileSize&) const = default;
};

// InvalidValue unless both sides are finite and from kMinTileSize to kMaxTileSize.
Status checkTileSize(const TileSize& size);

// "1.2x0.6" or "1.2" (square), metres, as the CLI takes it. InvalidValue when malformed or out of range.
Status parseTileSize(std::string_view text, TileSize& out);

// Pixel size of the base color map.
struct TextureSize {
    int width = 0;
    int height = 0;

    bool operator==(const TextureSize&) const = default;
};

// The map files as they were when last measured: the sum of their sizes and the latest write time. A
// difference on disk means a map changed and the material is cleaned and rendered again.
struct MaterialFiles {
    long long bytes = 0;
    SessionTime modified{};

    bool operator==(const MaterialFiles&) const = default;
};

struct MaterialInfo {
    std::string id;
    std::string name;
    std::vector<std::string> groups;  // group ids; empty means uncategorized
    std::vector<std::string> tags;    // sorted, no repeats
    std::string license = std::string(kUnknownLicenseId);
    std::string author;               // who made it, empty when unknown; info.json "author", optional
    AssetSource source;               // kind is kSourceTextureSet, kSourceBlendMaterial and so on
    TileSize tileSize;
    MaterialMaps maps;
    TextureSize textureSize;
    MaterialFiles files;
    AssetThumbnail thumbnail;         // engine is always "eevee"
    std::string blenderVersion;
    std::vector<BlenderWarning> warnings;

    bool operator==(const MaterialInfo&) const = default;
};

// True when the material has no opacity map.
bool isOpaque(const MaterialInfo& info);

struct MaterialInfoRead {
    Status status;
    MaterialInfo info;
};

// Reads and checks info.json with the same rules as readAssetInfo (ids, names, tags, the folder name), plus:
// base color, normal, roughness and metallic have a file, a map with origin None has no file and any other
// has one, files are relative and inside textures/, and the tile size passes checkTileSize. BadFile with a
// JSON pointer, CannotRead, UnsupportedVersion.
MaterialInfoRead readMaterialInfo(const std::filesystem::path& file, std::string_view folderName = {});

// Writes info.json through a temporary file and a rename. CannotWrite.
Status writeMaterialInfo(const std::filesystem::path& file, const MaterialInfo& info);

// The size and latest write time of the map files of an item folder. Files that are missing are skipped.
MaterialFiles measureMaterialFiles(const std::filesystem::path& folder, const MaterialMaps& maps);

}
