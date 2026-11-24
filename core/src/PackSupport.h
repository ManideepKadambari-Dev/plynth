#pragma once

#include "plynth/core/Pack.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/MaterialInfo.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Helpers shared by writing and reading packs.
namespace plynth::pack {

inline constexpr std::string_view kShapeSuffix = ".shape.json";
inline constexpr std::string_view kTrimProfileSuffix = ".profile.json";

// The folder inside pack/ that holds an entry's files, with a trailing slash: "assets/<id>/",
// "materials/<id>/" or "shapes/".
std::string packPrefix(PackKind kind, const std::string& id);

// The one file name a shape or trim profile has, for example "keyhole.shape.json". Empty for other kinds.
std::string shapeFileName(PackKind kind, const std::string& id);

// Says whether a relative path from a pack may be used: forward slashes only, no drive, no colon, no "." or
// ".." part, no empty part, no reserved Windows name, no part ending in a dot or space, no control character.
// why receives the reason when it may not.
bool isSafeRelative(std::string_view text, std::string* why = nullptr);

// Says whether a file type may be in a pack: JSON, Blender, FBX and the usual image types.
bool allowedFileType(std::string_view text);

// Says whether a path is a link or junction, or any other reparse point. Such a path is never followed.
bool isLink(const std::filesystem::path& path);

// The files of an asset, relative to its item folder, sorted: info.json, asset.blend, asset.fbx and thumb.png
// when they are there, and every texture and map info.json lists. Empty with why filled when a listed file is
// missing, a link, or has an unsafe path.
std::vector<std::string> assetFiles(const std::filesystem::path& folder, const AssetInfo& info, std::string* why);

// The files of a material, relative to its item folder, sorted: info.json, thumb.png when it is there, and every
// map info.json lists. Empty with why filled as for assetFiles.
std::vector<std::string> materialFiles(const std::filesystem::path& folder, const MaterialInfo& info, std::string* why);

// Copies a file and gives the copy the source's write time.
bool copyKeepingTime(const std::filesystem::path& from, const std::filesystem::path& to);

// Says whether two files hold the same bytes.
bool sameBytes(const std::filesystem::path& a, const std::filesystem::path& b);

// Eight hex digits, for names of staging folders.
std::string randomSuffix();

// The entry in its manifest form.
Json entryToJson(const PackEntry& entry);

}
