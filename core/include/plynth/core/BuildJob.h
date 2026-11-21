#pragma once

#include "BlenderJob.h"
#include "BuildGeometry.h"
#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The build job core sends to the Blender worker, and the result it reads back (task kBuildTask, see
// BlenderJob.h for the job and result envelope). The params carry plan.json unchanged, the shapes worked out
// by BuildGeometry.h, the asset files the items use, and the build settings. The worker reads nothing else.
//
// Params, schema/blender-job.schema.json "build":
//   {"project": {"name": "Flat", "folder": "D:/Projects/Flat"},
//    "output_folder": "D:/Projects/Flat/build", "blend_file": "building.blend", "fbx_file": "building.fbx",
//    "plan": {...plan.json...},
//    "settings": {"bevel_width": 0.01, "bevel_segments": 2, "fill_openings": true},
//    "assets": [{"id": "sofa", "blend_file": "D:/Library/assets/items/sofa/asset.blend", "collection": "sofa"}],
//    "floors": [{"id": "f1", "elevation": 0.0, "ceiling_height": 3.0,
//                "wall_solids": [{"height": 3.0, "outline": ring, "holes": [ring, ...]}],
//                "openings": [opening, ...], "rooms": [room, ...], "items": [item, ...]}]}
//   ring: {"points": [[x, y], ...], "edges": [{"wall": "w1", "side": "right", "room": "r1"}, ...]}
//         side is "left", "right" or null (an end face); room is null outside.
//   opening: {"id", "wall", "type", "shape", "width", "height", "sill", "hinge", "swing", "frame",
//             "centre": [x, y], "tangent": [x, y], "right": [x, y], "thickness", "curved",
//             "outline": [[u, v], ...], "inner_outline": [[u, v], ...],
//             "cutter": {"right": [[x, y, z], ...], "left": [[x, y, z], ...]}, "threshold": [[x, y], ...]}
//   room: {"id", "type", "area", "floor_z", "ceiling_z", "polygons": [{"outline": [[x, y], ...],
//          "holes": [[[x, y], ...], ...]}]}
//   item: {"id", "asset", "location": [x, y, z], "rotation_z", "size": {"width", "depth", "height"}, "wall"}
// The fields mean what the structs in BuildGeometry.h say; positions are world coordinates.
//
// Output in the result, schema/blender-result.schema.json "buildOutput":
//   {"blend_file": "building.blend", "fbx_file": "building.fbx", "objects": 42, "triangles": 18200,
//    "counts": {"wall_solids": 1, "openings_cut": 9, "fills": 7, "rooms": 5, "items": 4},
//    "steps": [{"step": "walls", "ms": 120}, ...],
//    "problems": [{"code": "cut_failed", "severity": "error", "message": "...", "floor": "f1",
//                  "object": "o3", "step": "openings"}]}
// The worker writes building.blend and building.fbx under a temporary name and renames them at the end, so a
// failed build leaves the files of the last good build in place.

inline constexpr std::string_view kBuildFolderName = "build";
inline constexpr std::string_view kBuildBlendFileName = "building.blend";
inline constexpr std::string_view kBuildFbxFileName = "building.fbx";

// The finished-look settings of DESIGN.md section 7.13a that are fixed for now.
inline constexpr double kBuildBevelWidth = 0.01; // metres; 0 turns the bevel off
inline constexpr int kBuildBevelSegments = 2;

struct BuildSettings {
    double bevelWidth = kBuildBevelWidth;
    int bevelSegments = kBuildBevelSegments;
    bool fillOpenings = true; // put a door or window in every door and window opening
    bool writeFbx = true;     // export building.fbx for the 3D preview

    bool operator==(const BuildSettings&) const = default;
};

// How bad a build problem is.
enum class BuildSeverity {
    Error,   // "error": the object was left out of the build or is incomplete
    Warning, // "warning": it was built, but should be checked
};

// "error" or "warning".
std::string_view buildSeverityName(BuildSeverity severity);

// The severity for a name used in files. Empty when the name is unknown.
std::optional<BuildSeverity> buildSeverityFromName(std::string_view name);

// Problem codes found by core before Blender starts (step "check"):
//   opening_does_not_fit (error, the opening is not cut; the message is checkOpeningFit's),
//   room_not_closed (error, no floor or ceiling), missing_asset (error, the item is left out),
//   asset_broken (error, the asset is Broken or its asset.blend is gone; the item is left out),
//   no_library (error, no library is set or it cannot open; every item is left out, object empty),
//   and every other itemProblemName (warnings: no_room, outside_room, overlaps_item, overlaps_wall,
//   overlaps_opening, off_wall, too_tall, size_changed).
// Codes the worker steps report: cut_failed (error), fill_failed (warning, the opening is cut but empty),
// asset_load_failed (error), bevel_failed (warning, the walls are left sharp). New steps may add codes.
inline constexpr std::string_view kCheckStep = "check";

struct BuildProblem {
    std::string code;
    BuildSeverity severity = BuildSeverity::Error;
    std::string message; // one plain sentence, for example "Door o3 does not fit its wall: it reaches 0.1 m past the corner."
    Id floor;            // empty when it is not about one floor
    Id object;           // the plan id the app marks: a point, wall, room, opening or item; empty for none
    std::string step;    // kCheckStep, or the worker step that found it

    bool operator==(const BuildProblem&) const = default;
};

// What the build needs to know about one asset from the library.
struct BuildAssetState {
    std::filesystem::path blendFile; // items/<id>/asset.blend
    bool broken = false;             // AssetState::Broken, or asset.blend is not there
    std::string problem;             // why, when broken
};

// Says what the library has for an asset id. Empty when the asset is not in the library.
using BuildAssetSource = std::function<std::optional<BuildAssetState>(std::string_view assetId)>;

// An asset file the job lists. Its objects are in the collection named after the id (AssetInfo.h).
struct BuildAsset {
    std::string id;
    std::filesystem::path blendFile;

    bool operator==(const BuildAsset&) const = default;
};

// Everything worked out before Blender starts.
struct BuildPlan {
    std::vector<FloorBuild> floors;   // in plan order
    std::vector<BuildAsset> assets;   // each asset an item that is built uses, once, sorted by id
    std::vector<BuildProblem> problems; // in floor order, then walls, openings, rooms, items, each in plan order

    bool operator==(const BuildPlan&) const = default;
};

// Works out the build plan of a project: floorElevations, then for every floor wallSolids, openingBuild for
// each opening (one that does not fit gives opening_does_not_fit), roomBuild for each room (room_not_closed),
// and the items whose asset the source has and is not broken (missing_asset, asset_broken), with itemProblems
// for the rest as warnings. An empty source means there is no library: no_library and no items.
BuildPlan makeBuildPlan(const Project& project, const BuildAssetSource& assets);

// A problem in its file form: {"code", "severity", "message", "floor", "object", "step"}, with null for an
// empty floor or object. The worker's result, build.json and "plynth build --check" all use it.
Json buildProblemToJson(const BuildProblem& problem);

// Says whether a plan has anything to build. InvalidValue "The plan has no walls to build." when no floor
// has a wall.
Status checkBuildable(const Plan& plan);

// Makes the build job: the envelope of makeBlenderJob with task kBuildTask and the params above. outputFolder
// is projectFolder / kBuildFolderName. fbx_file is null when settings.writeFbx is false.
Json makeBuildJob(const Project& project, const BuildPlan& plan, const BuildSettings& settings,
                  const std::filesystem::path& projectFolder, std::string_view jobId,
                  const std::filesystem::path& resultFile);

// How long one worker step took.
struct BuildStepTime {
    std::string step;
    long long ms = 0;

    bool operator==(const BuildStepTime&) const = default;
};

// What the worker made.
struct BuildCounts {
    int wallSolids = 0;
    int openingsCut = 0;
    int fills = 0;
    int rooms = 0;
    int items = 0;

    bool operator==(const BuildCounts&) const = default;
};

// The build task's result, read and checked. BadFile when a field is missing or has the wrong type, or a
// severity is unknown.
struct BuildWorkerOutput {
    BlenderResultHeader header;
    std::string blendFile;  // file name inside the output folder
    std::string fbxFile;    // file name inside the output folder, empty when none was asked for
    long long objects = 0;
    long long triangles = 0;
    BuildCounts counts;
    std::vector<BuildStepTime> steps;
    std::vector<BuildProblem> problems;
};

BuildWorkerOutput readBuildResult(const Json& result, std::string_view jobId);

}
