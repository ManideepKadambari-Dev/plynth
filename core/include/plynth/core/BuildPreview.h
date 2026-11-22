#pragma once

#include "Plan.h"
#include "Status.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace plynth {

// The preview parts of a build (milestone 12): one FBX per floor and one per floor's roofs, written by the
// build step preview_parts into build/preview/ beside building.fbx, with the same materials and images (so
// build/materials.json applies to them too). The app's 3D view loads them to hide roofs or the floors above the
// one shown, and the plan overlay loads only the shown floor's part. Without parts.json (an older build, or
// preview_failed) the app loads building.fbx as before.
//
// build/preview/parts.json, schema/build-preview.schema.json:
//   {"format": "plynth-build-preview", "version": 1,
//    "parts": [{"floor": "f1", "kind": "storey", "file": "f1.fbx"}, {"floor": "f1", "kind": "roof",
//               "file": "f1_roof.fbx"}, ...]}
// Parts are in floor order, the storey before the roof. A floor without roof sections has no roof part. File
// names are inside build/preview/. Everything of a floor's collection except roofs (and collision, from
// milestone 10) goes into its storey part: walls, thresholds, fills, floors, ceilings, items, the slab and the
// stairs that climb from it.

inline constexpr std::string_view kBuildPreviewFolder = "preview";
inline constexpr std::string_view kPreviewPartsFileName = "parts.json";
inline constexpr std::string_view kPreviewPartsFormat = "plynth-build-preview";
inline constexpr int kPreviewPartsVersion = 1;

enum class PreviewPartKind {
    Storey, // "storey"
    Roof,   // "roof"
};

std::string_view previewPartKindName(PreviewPartKind kind);
std::optional<PreviewPartKind> previewPartKindFromName(std::string_view name);

struct PreviewPart {
    Id floor;
    PreviewPartKind kind = PreviewPartKind::Storey;
    std::filesystem::path file; // absolute

    bool operator==(const PreviewPart&) const = default;
};

struct PreviewPartsRead {
    Status status; // NotFound without parts.json, BadFile when it breaks the schema or a file name has a folder
    std::vector<PreviewPart> parts;
};

// Reads buildFolder/preview/parts.json and makes the file paths absolute. Files that are not there are left
// out.
PreviewPartsRead readPreviewParts(const std::filesystem::path& buildFolder);

}
