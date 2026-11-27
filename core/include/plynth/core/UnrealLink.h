#pragma once

#include "EngineLink.h"
#include "Export.h"
#include "ExportGroups.h"
#include "Project.h"
#include "Status.h"
#include "UnrealRemote.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The Unreal Engine link (DESIGN.md section 7.16, owner 2026-11-27, issue 36): the engine link with id
// kUnrealLinkId, in core/src/links/engines/UnrealLink.cpp (the job and result in core/src/links/unreal/). It
// finds editors with findUnrealEditors (UnrealRemote.h), its prepare step is runExport with the unreal profile
// (ExportJob.h), and it delivers by writing an Unreal job file and running the Plynth script inside the editor:
//   runUnrealCommand(editor, "\"<unreal scripts>/plynth_link.py\" \"<work>/unreal_job.json\"", ExecuteFile)
// The script (engines/unreal/plynth_link.py, run by Unreal's own Python) imports what the job lists, writes the result
// file the job names, and prints one line "plynth_link: ok" or "plynth_link: error <code>". Core reads the result
// file; a missing or bad one is EngineFailed with the last output lines.
//
// Where it goes: in the open project, Content/Plynth/<Building>/ (made with Content/Plynth/ when missing), where
// Building is exportFileStem of the project name:
//   Meshes/     SM_<Building> (one building asset), or SM_<Building>_<Group> per group when grouped
//   Kit/        SM_Kit_<id>, when the kit is on
//   Textures/   T_<Material>_<Channel>, only with Pack with materials
//   Materials/  M_Plynth_Opaque, M_Plynth_Masked, M_Plynth_Translucent (made by the script) and
//               MI_<Slot> per material slot, only with Pack with materials
//   Data/       the gameplay data and license report files, copied as files on disk next to the assets
// Every asset gets the metadata tags PlynthBuilding (the building name), PlynthProject (the project folder) and
// PlynthJob. A building folder whose PlynthProject names another project folder is never written into:
// EngineFailed "Content/Plynth/<Building> belongs to the Plynth project <folder>. Rename this project.".
// Updates import over the same asset paths (reimport in place), so actors that use them keep their place; an
// asset of this building that the new job no longer lists is deleted.
// With Pack with materials off, meshes keep their named material slots with Unreal's default material, and no
// texture or material asset is made (assets of an earlier run with it on are deleted).
// The level: every mesh of the job has one actor in the open level, label <Building>_<Group> (or <Building>),
// tag "Plynth:<Building>", at the origin the first time. An actor of this building that exists is kept where the
// user moved it; one whose mesh is gone is removed. The level is not saved; the editor marks it changed.

inline constexpr std::string_view kUnrealContentRoot = "/Game/Plynth";
inline constexpr std::string_view kUnrealLinkScript = "plynth_link.py";
inline constexpr std::string_view kUnrealJobFileName = "unreal_job.json";
inline constexpr std::string_view kUnrealResultFileName = "unreal_result.json";
inline constexpr std::string_view kUnrealJobFormat = "plynth-unreal-job";
inline constexpr std::string_view kUnrealResultFormat = "plynth-unreal-result";
inline constexpr int kUnrealJobVersion = 1;

// The Plynth scripts Unreal runs (the engines/unreal/ folder of the repository): the first folder holding
// kUnrealLinkScript of the PLYNTH_UNREAL_SCRIPTS environment variable, the folder "unreal" next to the running
// program, and in a developer build the source tree's engines/unreal/ folder. Empty when none does.
inline constexpr std::string_view kUnrealScriptsVariable = "PLYNTH_UNREAL_SCRIPTS";
std::filesystem::path unrealScriptsFolder();

// The building's folder name in Content/Plynth: exportFileStem(projectName).
std::string unrealBuildingName(std::string_view projectName);

// What the job asks the script to check after the import, for the headless test. Empty in a normal delivery.
struct UnrealJobChecks {
    bool enabled = false;
    std::optional<ExportBounds> expectedBounds; // world bounds of the walls in metres (ExportJob.h)
    std::filesystem::path screenshot;           // where to save a picture of the level; empty for none
};

// Makes the job file the script reads, in the form of schema/unreal-job.schema.json:
//   {"format": "plynth-unreal-job", "version": 1, "job_id": "link-3f9a07c1", "plynth_version": "0.2.0",
//    "building": "My_Flat", "project_folder": "D:/Projects/My Flat",
//    "content_path": "/Game/Plynth/My_Flat", "source_folder": "D:/Projects/My Flat/build/links/unreal/out",
//    "pack_materials": true, "grouped": false,
//    "meshes": [{"group": "building", "file": "My_Flat.fbx", "asset": "SM_My_Flat", "actor": "My_Flat"}],
//    "kit": null or {"file": "My_Flat_Kit.fbx", "pieces": ["SM_Kit_Wall_50x300x20_3f9a07", ...]},
//    "textures": [{"file": "Textures/T_OakPlanks_BaseColor.png", "asset": "T_OakPlanks_BaseColor",
//                  "channel": "base_color", "srgb": true}],
//    "materials": [{"slot": "Living_Floor", "asset": "MI_Living_Floor",
//                   "blend": "opaque" | "masked" | "translucent", "two_sided": false,
//                   "maps": {"base_color": "T_OakPlanks_BaseColor", "normal": ..., "roughness": ...,
//                            "metallic": ..., "opacity": ..., "occlusion": ...} (null for a map it has none of),
//                   "values": {"base_color": [r, g, b], "roughness": 0.5, "metallic": 0.0, "opacity": 1.0}}],
//    "data_files": ["My_Flat_gameplay.json", "My_Flat_licenses.json"],
//    "level": {"place_actors": true, "tag": "Plynth:My_Flat"},
//    "checks": null or {"expected_bounds": {"min": [x, y, z], "max": [x, y, z]}, "screenshot": "D:/.../shot.png"},
//    "result_file": "D:/Projects/My Flat/build/links/unreal/unreal_result.json"}
// textures and materials are empty when pack_materials is false. blend is translucent for a material with an
// opacity map or value below 1 that is glass (its slot surface is Glass), masked for any other opacity map, and
// opaque otherwise. Paths are absolute, UTF-8, forward slashes; file entries are inside source_folder.
// meshes are the export's group files: file as written, asset "SM_" and its stem, actor its stem, group the
// group id (the walls group is "structure"). slot is the FBX material name: the slot name of a built surface, or
// <Asset in PascalCase>_<material name> for furniture. A map is the texture the export copied for that channel,
// T_<Material>_<Channel> (T_<slot>_<Channel> for a neutral look or furniture), null when there is none.
// data_files also lists the kit layout when the kit was written.
Json makeUnrealJob(const EngineDelivery& delivery, const UnrealJobChecks& checks,
                   const std::filesystem::path& resultFile);

// One check the script ran (headless test only). status "pass", "warning" or "fail". Ids: "scale" (the meshes'
// box in centimetres holds expected_bounds times 100, Y mirrored as Unreal's FBX import does, within 0.5 cm,
// and reaches past it by at most 50 cm for the walls group "structure" when grouped, or 300 cm for the whole
// building otherwise, as roof overhangs and steps reach past the walls), "normals" (no mesh has degenerate or
// missing normals), "collision" (every static mesh has simple collision), "textures" (every texture asset
// exists, normal maps use normal map compression, data maps are not sRGB), "material_slots" (every mesh's slot
// names are the job's), "actors" (one actor per mesh with the building's tag).
struct UnrealCheck {
    std::string id;
    std::string status;
    std::string message;

    bool operator==(const UnrealCheck&) const = default;
};

// The result file, in the form of schema/unreal-result.schema.json:
//   {"format": "plynth-unreal-result", "version": 1, "job_id", "ok": true, "error": null or {"code", "message"},
//    "engine_version": "5.8.0", "project_name": "MyGame", "content_path": "/Game/Plynth/My_Flat",
//    "assets": [{"path", "kind", "action"}], "actors": [{"label", "asset", "action"}],
//    "checks": [{"id", "status", "message"}], "screenshot": null or "D:/.../shot.png", "ms": 5400}
// Error codes: "not_editor" (run outside the editor), "bad_job", "folder_taken" (the folder belongs to another
// project), "import_failed", "material_failed", "level_failed".
struct UnrealResult {
    Status status; // BadFile when a field is missing or has the wrong type, or job_id differs
    bool ok = false;
    std::string errorCode;
    std::string errorMessage;
    std::string engineVersion;
    std::string projectName;
    std::string contentPath;
    std::vector<EngineAssetResult> assets;
    std::vector<EngineActorResult> actors;
    std::vector<UnrealCheck> checks;
    std::filesystem::path screenshot;
    long long ms = 0;
};

UnrealResult readUnrealResult(const Json& result, std::string_view jobId);

// The Unreal Editor command line programs of an install folder, for the headless test only: Plynth itself never
// starts Unreal. UnrealEditor-Cmd.exe and UnrealEditor.exe under <root>/Engine/Binaries/Win64. Empty paths when
// they are not there.
struct UnrealInstall {
    std::filesystem::path root;
    std::filesystem::path editorCmd;
    std::filesystem::path editor;
};
UnrealInstall unrealInstallAt(const std::filesystem::path& root);

}
