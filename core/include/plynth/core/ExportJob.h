#pragma once

#include "BlenderJob.h"
#include "Build.h"
#include "BuildGeometry.h"
#include "BuildJob.h"
#include "Exporter.h"
#include "Kit.h"
#include "catalog/MaterialInfo.h"
#include "Project.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The export job core sends to the Blender worker, and the result it reads back (task kExportTask, see
// BlenderJob.h for the envelope). The worker opens build/building.blend (never saving it), so hand edits the
// user made there are exported too (DESIGN.md section 7.13), makes the objects ready for the job's profile, runs
// the quality checks of that profile (DESIGN.md section 7.17) and writes the FBX files and Textures/ into an
// empty staging folder. Core moves them into place afterwards (Export.h).
//
// Profiles (owner, 2026-11-27): the build is the same for every use, and the export profile decides what the
// worker makes of it. Profiles are registry entries on the Blender side too (blender/plynth/exporting/profiles/,
// one file each); core names one by id in "profile".
//   plain   the Export button and "plynth export": an FBX for any use. Objects keep the build's plain names
//           (Name rules below), no collision mesh is written, materials keep their slot names, textures are
//           <Material>_<Channel>.<ext>. No kit and no gameplay data. Checks: build_current, build_complete,
//           scale, transforms, naming (plain rules), uvs, texel_density, triangles, materials, groups, fbx_scale.
//   unreal  what the Unreal Engine link sends (EngineLink.h, UnrealLink.h): every group's objects joined into
//           one mesh, SM_<Stem> for a single export or SM_<Stem>_<Group name> for a grouped one (characters other
//           than letters, digits and "_" as "_"), with every collision piece of its objects as
//           UCX_<mesh>_<NN>; textures T_<Material>_<Channel>.<ext>; the kit when asked (kit pieces SM_Kit_<id>
//           stay one mesh each). Every check, the naming check with the SM_ and UCX_ rules, collision and
//           kit_file included.
// Both write with the one FBX profile of blender/plynth/exporters/fbx.py (metres, Y up, applied transforms),
// so every group shares the building origin.
//
// Name rules. The build names every object plainly, with no engine prefix: <floor>_Walls, <floor>_Slab,
// <floor>_<stair>_Stair, <floor>_Roof_<NN>, <asset>_<item>, <generator>_<item>, <floor>_Skirting and so on
// (BuildJob.h), and its collision meshes <object>_Collision_<NN>, plynth_kind "collision", in the collection
// "Collision". A building.blend made before this rule (names starting with SM_, collision UCX_) exports the
// same: the prepare step drops a leading "SM_" and finds collision by plynth_kind, never by name.
//
// Params, schema/blender-job.schema.json "export":
//   {"project": {"name": "Flat", "folder": "D:/Projects/Flat"},
//    "profile": "plain" or "unreal",
//    "blend_file": "D:/Projects/Flat/build/building.blend",
//    "output_folder": "D:/Projects/Flat/export/.staging", "textures_folder": "Textures",
//    "groups": [{"id": "building", "file": "Flat.fbx", "kinds": [], "ids": [],
//                "name": "Flat", "members": [{"kind": "walls", "id": "f1"}, {"kind": "slab", "id": null}],
//                "rest": false}],
//              (name, members and rest are optional in the schema and always written by core;
//               an id of null is an empty ExportMember::id; Exporter.h says which group an object goes to)
//    "materials": [...the "materials" list of build/materials.json, each catalog material with "tile_size":
//                  {"width", "height"}: the built one, else the library's, else left out...],
//    "expected_bounds": {"min": [x, y, z], "max": [x, y, z]} or null,
//    "build": {"report_found": true, "plan_changed": false, "problems": [buildProblem, ...]},
//    "limits": {"object_triangles": 1000000, "total_triangles": 10000000, "low_texel_density": 256,
//               "texel_warning_ratio": 0.5, "texel_fail_ratio": 0.25, "bounds_tolerance": 0.001},
//    "force": false, "write": true,
//    "kit": null or {"file": "Flat_Kit.fbx", "fill_openings": true,
//                    "bevel": {"width": 0.01, "segments": 2},
//                    "pieces": [{"id": "Wall_50x300x20_3f9a07", "mesh": "SM_Kit_Wall_50x300x20_3f9a07",
//                                "kind": "wall", "wall_solids": [wallSolid with kit rings], "openings": [opening],
//                                "fills": ["o3"], "collision": [{"outline", "bottom", "top"}]}]}}
//   (kit is optional in the schema: missing means null. Each piece is in its own frame, Kit.h; wall_solids and
//    openings have the build job's form (BuildJob.h), and ring edges may have "kit_cut": true. fills lists the
//    openings whose door or window this piece gets.)
//
// What the worker does, in order (blender/plynth/exporting/, one registered step per file):
//   open_blend      opens blend_file; no mesh with a plynth_kind in it is no_geometry
//   prepare         makes item instances real and joins each item's parts into one mesh named after the item
//                   object (<asset>_<item>); gives every mesh its own data; applies every transform and clears
//                   parents, so every object sits at the building origin with an identity transform and all
//                   files line up (DESIGN.md section 7.16); finds collision meshes by plynth_kind "collision"
//                   and drops a leading "SM_" from the names of an older build
//   name_objects    by the profile: plain drops the collision meshes and keeps every mesh's plain name;
//                   unreal names every mesh SM_<name> and every collision mesh UCX_<its mesh>_<NN>. In both,
//                   characters other than letters, digits and "_" become "_", a name already taken gets _2, _3
//                   and so on, furniture materials become <Asset>_<material name> so two assets never share a
//                   slot name, and built slots keep their names (MaterialSlots.h). The scene checks see each
//                   built object on its own
//   kit_pieces      (order 35) when kit is not null: makes every piece's meshes in a collection "Kit" away from
//                   the building (the build's wall, cut, fill and bevel code on the piece's solids and openings;
//                   kit cut edges not bevelled), names them by mesh, gives them the materials of their slots
//                   from building.blend, and makes their UCX_ meshes; plynth_kind "kit", plynth_id the piece id.
//                   They are checked with everything else and never go into a group. Only the unreal profile
//                   gets a kit
//   textures        copies every image the exported materials use into output_folder/Textures/ and points the
//                   image nodes at the copies, each name starting with the profile's prefix (none for plain, T_
//                   for unreal): <Material>_<Channel>.<ext> for a catalog material (material id in PascalCase;
//                   channels BaseColor, Normal, Roughness, Metallic, Opacity, Occlusion), the slot name instead
//                   of the material for a neutral look, <Asset>_<Material>_<Channel> for a furniture material's
//                   maps (AssetInfo.h; full size, never the preview copies), <material name>_<image file stem>
//                   for anything a user added by hand in building.blend; a name already taken gets _2, _3 and so
//                   on; a file is copied byte for byte and once, however many materials use it
//   checks          runs every quality check of phase "scene" that the profile lists
//   finish_objects  (order 55) by the profile, and only when the files will be written: plain leaves every object
//                   as it is; unreal joins the meshes of each group into one mesh named after the group's file,
//                   SM_<Stem> for a single export or SM_<Stem>_<Group> for a grouped one, with the group's
//                   collision meshes as UCX_<that mesh>_<NN>; lights stay apart, kit pieces are left alone, and
//                   a group's collision without any mesh is left out
//   write_fbx       when write is true and no scene check failed (or force is true): every group into its
//                   file in output_folder, with the one FBX profile and relative texture paths; a group that
//                   gets no object is not written and is listed in empty_groups
//   write_kit       (order 65) when kit is not null and write_fbx wrote: every kit piece and its collision into
//                   kit.file in output_folder, with the same profile
//   file_checks     runs every check of phase "file" that the profile lists on the written files; skipped when
//                   nothing was written
// A check never stops the job: it reports pass, warning, fail or skipped. The job fails only for errors such as
// a missing blend file (import_failed), no_geometry or export_failed.
//
// Quality checks (blender/plynth/quality/, one registered file each, in this order). Each names the profiles it
// runs for; collision and kit_file run for unreal only, every other check for both:
//   build_current   "Build is up to date": fail when the plan changed since the build or there is no build
//                   report
//   build_complete  "Build is complete": fail for every error of the last build (left out items, cuts that
//                   failed, rooms that do not close, slabs, stairs and roofs that could not be made); each finding
//                   names its plan id
//   scale           "Scale and units": fail unless the scene is metric with unit scale 1 and the bounds of the
//                   walls match expected_bounds within bounds_tolerance
//   transforms      "Pivots and applied transforms": fail for an exported object whose transform is not the
//                   identity, or that has a parent
//   naming          "Naming": fail for a mesh name that breaks the profile's rule (unreal: building meshes
//                   start with SM_ and collision meshes with UCX_; plain: no name starts with SM_ or UCX_), a
//                   repeated name, a character other than letters, digits and "_", a collision mesh whose object
//                   is not exported, a slot or texture name breaking the same rules
//   uvs             "UVs": fail for a textured mesh without UVs; warning for faces whose UVs have no area, and
//                   for furniture whose UVs overlap (building surfaces tile on purpose, so they are not
//                   checked for overlap)
//   texel_density   "Texture sharpness": pixels per metre of each material's base color on the surfaces it
//                   covers; warning below low_texel_density or below texel_warning_ratio of the median, fail
//                   below texel_fail_ratio of the median; constant images (kConstantMapSize) are skipped
//   triangles       "Triangle counts": fail for a mesh with no triangles; warning above object_triangles for
//                   one mesh or total_triangles in all
//   materials       "Materials and textures": fail for a mesh without materials, a face on an empty slot, a
//                   slot with the neutral look (no material assigned, or one that failed to load), an image
//                   file that is missing, or a base color that is not an image (a procedural material: Plynth
//                   cannot bake it, because baking needs Cycles)
//   collision       "Collision", unreal only: fail for a mesh of plynth_kind walls, floor, slab, stair, roof,
//                   item, kit or generator without a UCX_ mesh, each object on its own, so every roof section
//                   needs one (ceilings, thresholds and door and window fills need none), and for a UCX_ mesh
//                   that is not closed or not convex
//   groups          (order 105) "Groups": fail for a building object no group takes, naming it; warning
//                   for each group that gets no object (it is not written); pass otherwise
//   fbx_scale       phase "file": reads each written group FBX back into an empty scene; fail unless the walls'
//                   bounds (over all files together) match expected_bounds within bounds_tolerance and each
//                   file's object count matches
//   kit_file        (order 120) phase "file", "Modular kit", unreal only: skipped without a kit or when
//                   nothing was written; reads kit.file back; fail unless every piece's mesh is in it once, at
//                   the origin, with at least one UCX_ mesh
//
// Output in the result, schema/blender-result.schema.json "exportOutput":
//   {"written": true,
//    "files": [{"group": "building", "file": "Flat.fbx", "objects": 40, "triangles": 18200}],
//    "textures": ["OakPlanks_BaseColor.jpg", ...] (T_OakPlanks_BaseColor.jpg for unreal),
//    "objects": 40, "triangles": 18200, "collision_objects": 31,
//    "checks": [{"id": "naming", "name": "Naming", "phase": "scene", "status": "pass", "message": "...",
//                "findings": [{"object": "f1_Walls", "plan_id": "w3", "message": "..."}]}],
//    "steps": [{"step": "open_blend", "ms": 410}, ...],
//    "empty_groups": ["furniture_r4"],
//    "kit": null or {"file": "Flat_Kit.fbx", "pieces": 14, "objects": 40, "triangles": 3100}}
// files and textures are empty when written is false. findings holds at most kMaxQualityFindings entries per
// check; the message then says how many there were. empty_groups and kit are optional in the schema (missing
// reads as none); kit is null unless the kit was written. objects, triangles and collision_objects count the
// building objects only, not the kit; collision_objects is 0 for the plain profile.

inline constexpr std::string_view kExportTexturesFolder = "Textures";
inline constexpr int kMaxQualityFindings = 50;

// The numbers the checks use. One place, passed in the job.
inline constexpr long long kObjectTriangleWarning = 1000000;
inline constexpr long long kTotalTriangleWarning = 10000000;
inline constexpr double kTexelWarningRatio = 0.5;
inline constexpr double kTexelFailRatio = 0.25;
inline constexpr double kBoundsTolerance = 0.001; // metres

enum class QualityStatus {
    Pass,    // "pass"
    Warning, // "warning": does not block
    Fail,    // "fail": blocks the export unless it is forced
    Skipped, // "skipped": did not run (a file check when nothing was written)
};

std::string_view qualityStatusName(QualityStatus status);
std::optional<QualityStatus> qualityStatusFromName(std::string_view name);

enum class QualityPhase {
    Scene, // "scene": on the objects ready to export, before writing
    File,  // "file": on the written files
};

std::string_view qualityPhaseName(QualityPhase phase);
std::optional<QualityPhase> qualityPhaseFromName(std::string_view name);

// One thing a check found.
struct QualityFinding {
    std::string object; // the exported object or slot it is about; empty for none
    Id planId;          // the plan id the app can select; empty for none
    std::string message;

    bool operator==(const QualityFinding&) const = default;
};

struct QualityCheckResult {
    std::string id;      // for example "naming"
    std::string name;    // for example "Naming"
    QualityPhase phase = QualityPhase::Scene;
    QualityStatus status = QualityStatus::Pass;
    std::string message; // one plain sentence
    std::vector<QualityFinding> findings;

    bool operator==(const QualityCheckResult&) const = default;
};

// What the last build says, for the build_current and build_complete checks.
struct ExportBuildState {
    bool reportFound = false;
    bool planChanged = true;
    std::vector<BuildProblem> problems;

    bool operator==(const ExportBuildState&) const = default;
};

struct ExportBounds {
    Vec3 min;
    Vec3 max;

    bool operator==(const ExportBounds&) const = default;
};

// The world bounds of every wall solid of the project (wallSolids of each floor raised from its elevation to its
// height). Empty when the plan has no walls.
std::optional<ExportBounds> expectedWallBounds(const Project& project);

// The export profiles above. Names in files: "plain", "unreal".
enum class ExportProfile { Plain, Unreal };

std::string_view exportProfileName(ExportProfile profile);
std::optional<ExportProfile> exportProfileFromName(std::string_view name);

struct ExportSettings {
    bool force = false; // write even when checks fail (DESIGN.md section 7.17: "unless the user overrides them")
    bool write = true;  // false runs the checks only and writes nothing
    ExportProfile profile = ExportProfile::Plain;

    bool operator==(const ExportSettings&) const = default;
};

// Makes the export job: the envelope of makeBlenderJob with task kExportTask and the params above. blend_file is
// projectFolder/build/building.blend, output_folder is stagingFolder, profile is settings.profile. Paths are
// absolute, UTF-8, forward slashes. The plain profile always writes "kit": null. kit null writes "kit": null; otherwise kit.file is exportFileStem(project name) + kKitFileSuffix,
// fill_openings and bevel are the build's defaults (BuildSettings). A materials entry's "tile_size", for the kit
// UVs, is its BuiltMaterial::tileSize, the size it was built with, which always wins. tileSizes (catalog material
// id to tile size in metres, from the library) is only used for an entry that has none.
Json makeExportJob(const Project& project, const std::vector<ExportGroup>& groups,
                   const std::vector<BuiltMaterial>& materials, const ExportBuildState& build,
                   const ExportSettings& settings, const std::filesystem::path& projectFolder,
                   const std::filesystem::path& stagingFolder, std::string_view jobId,
                   const std::filesystem::path& resultFile, const ModularKit* kit = nullptr,
                   const std::map<std::string, TileSize>* tileSizes = nullptr);

// One file the worker wrote.
struct ExportFileOutput {
    std::string group;
    std::string file; // the file name inside the staging folder
    long long objects = 0;
    long long triangles = 0;
    std::string name; // the group's name; filled by core from its groups, never read from the result

    bool operator==(const ExportFileOutput&) const = default;
};

// The kit file the worker wrote.
struct ExportKitOutput {
    std::string file; // the file name inside the staging folder
    int pieces = 0;
    long long objects = 0; // piece meshes and their collision
    long long triangles = 0;

    bool operator==(const ExportKitOutput&) const = default;
};

// The export task's result, read and checked. BadFile when a field is missing or has the wrong type, a status or
// phase is unknown, or a file name has a folder in it.
struct ExportWorkerOutput {
    BlenderResultHeader header;
    bool written = false;
    std::vector<ExportFileOutput> files;
    std::vector<std::string> textures; // file names inside Textures/
    long long objects = 0;
    long long triangles = 0;
    long long collisionObjects = 0;
    std::vector<QualityCheckResult> checks;
    std::vector<BuildStepTime> steps;
    std::vector<std::string> emptyGroups;  // group ids that got no object; empty when the result has none
    std::optional<ExportKitOutput> kit;    // empty unless the kit was written
};

ExportWorkerOutput readExportResult(const Json& result, std::string_view jobId);

}
