#pragma once

#include "Plan.h"
#include "Status.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace plynth {

// The preview parts of a build: one FBX per floor, one for the slab above it and one for its roofs, written by
// the build step preview_parts into build/preview/ beside building.fbx, with the same materials and images (so
// build/materials.json applies to them too). The app's 3D view loads them to hide roofs, or to cut the building
// at the shown floor, and the plan overlay loads only the shown floor's storey part. Without parts.json (an
// older build, or preview_failed) the app loads building.fbx as before.
//
// build/preview/parts.json, schema/build-preview.schema.json:
//   {"format": "plynth-build-preview", "version": 1,
//    "parts": [{"floor": "f1", "kind": "storey", "file": "f1.fbx"}, {"floor": "f1", "kind": "slab",
//               "file": "f1_slab.fbx"}, {"floor": "f1", "kind": "roof", "file": "f1_roof.fbx"}, ...]}
// Parts are in floor order, then storey, slab, roof. A floor without roof sections has no roof part and one
// without a slab no slab part. File names are inside build/preview/. The slab part is the floor's slab object:
// its side faces, undersides and ledge tops. The roof part is its roofs. Collision is in no part. Everything
// else of the floor's collection goes into its storey part: walls, thresholds, fills, floors, ceilings, items
// and the stairs that climb from it.
// The cut at floor c hides the storey parts of the floors above c and the slab and roof parts of c and above.
// Hiding the roofs alone keeps every slab. Parts written without a slab part have the slab in the storey.

inline constexpr std::string_view kBuildPreviewFolder = "preview";
inline constexpr std::string_view kPreviewPartsFileName = "parts.json";
inline constexpr std::string_view kPreviewPartsFormat = "plynth-build-preview";
inline constexpr int kPreviewPartsVersion = 1;

enum class PreviewPartKind {
    Storey, // "storey"
    Roof,   // "roof"
    Slab,   // "slab": the slab above the floor
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
