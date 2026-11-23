#pragma once

#include "BlenderJob.h"
#include "BuildGeometry.h"
#include "BuildProblem.h"
#include "Collision.h"
#include "GeneratorBuild.h"
#include "MaterialSlots.h"
#include "Materials.h"
#include "Plan.h"
#include "Project.h"
#include "Status.h"
#include "catalog/MaterialInfo.h"

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
//    "materials_file": "materials.json", "textures_folder": "textures",
//    "preview_textures_folder": "preview_textures", "preview_texture_size": 1024,
//    "settings": {"bevel_width": 0.01, "bevel_segments": 2, "fill_openings": true},
//    "assets": [{"id": "sofa", "blend_file": "D:/Library/assets/items/sofa/asset.blend", "collection": "sofa",
//                "materials": [{"name": "Fabric", "maps": {"base_color": "D:/Library/assets/items/sofa/
//                               textures/maps/fabric/base_color.png", ..., "opacity": null, "occlusion": null}}]}],
//    "materials": [{"id": "oak_planks", "tile_size": {"width": 2.0, "height": 2.0},
//                   "maps": {"base_color": "D:/Library/materials/items/oak_planks/textures/base_color.jpg",
//                            "normal": "...", "roughness": "...", "metallic": "...",
//                            "opacity": null, "occlusion": null}}],
//    "preview_folder": "preview",
//    "slots": [{"name": "Living_Floor", "material": "oak_planks", "surface": "Floor"}, ...],
//    "generators": [{"id": "column", "file": "D:/Library/generators/column.py", "addons": []}],
//    "floors": [{"id": "f1", "elevation": 0.0, "ceiling_height": 3.0,
//                "wall_solids": [{"height": 3.0, "top_material": "white_paint", "top_slot": "Wall_Top",
//                                 "outline": ring, "holes": [ring, ...]}],
//                "openings": [opening, ...], "rooms": [room, ...], "items": [item, ...],
//                "slab": slab, "stairs": [stair, ...], "roofs": [roof, ...],
//                "collision": [{"target": {"kind": "walls", "id": "f1"},
//                               "pieces": [{"outline": [[x, y], ...], "bottom": 0.0, "top": 3.0}]}, ...],
//                "generator_items": [generator item, ...]}]}
//   ring: {"points": [[x, y], ...],
//          "edges": [{"wall": "w1", "side": "right", "room": "r1", "material": "white_paint",
//                     "slot": "Living_Wall"}, ...]}
//         side is "left", "right" or null (an end face); room is null outside.
//   opening: {"id", "wall", "type", "shape", "width", "height", "sill", "hinge", "swing", "frame",
//             "centre": [x, y], "tangent": [x, y], "right": [x, y], "thickness", "curved",
//             "outline": [[u, v], ...], "inner_outline": [[u, v], ...],
//             "cutter": {"right": [[x, y, z], ...], "left": [[x, y, z], ...]}, "threshold": [[x, y], ...],
//             "materials": {"reveal", "threshold", "door", "frame", "glass", "handle"},
//             "slots": {"reveal", "threshold", "door", "frame", "glass", "handle"}}
//   room: {"id", "type", "area", "floor_z", "ceiling_z", "floor_material", "ceiling_material",
//          "floor_slot", "ceiling_slot",
//          "polygons": [{"outline": [[x, y], ...], "holes": [[[x, y], ...], ...]}], "ceiling_polygons": [...]}
//   item: {"id", "asset", "location": [x, y, z], "rotation_z", "size": {"width", "depth", "height"}, "wall"}
//   generator item: {"id", "generator", "params", "location": [x, y, z], "rotation_z",
//          "size": {"width", "depth", "height"}, "room", "wall", "slots": {<material param>: slot name}}
//          see GeneratorBuild.h; both lists are always written, empty when there are no generators
//   slab: {"bottom", "top", "polygons": [polygon, ...], "undersides": [polygon, ...],
//          "sides": [{"outline": ["edge", "well", ...], "holes": [["edge", ...], ...]}, ...],
//          "tops": [polygon, ...], "materials": {"edge", "well", "underside"},
//          "slots": {"edge", "well", "underside"}}
//         sides has one entry per polygon and one name per ring edge (SlabPolygonSides); tops are the ledges,
//         faces at top with the edge material and the edge slot.
//   stair: {"id", "type", "to_floor", "rise", "riser_count", "riser", "material", "slot",
//           "steps": [{"part": "step", "outline": [[x, y], ...], "bottom", "top"}], "hole": polygon}
//   roof: {"index", "section", "type", "faces": [{"part": "covering", "outline": [[x, y, z], ...],
//          "holes": [[[x, y, z], ...], ...], "material", "slot"}]}
//   collision entry: {"target": {"kind", "id"}, "index", "pieces": [piece, ...],
//                     "hulls": [[[x, y, z], ...], ...]}
//         index is the roof's, 0 for other kinds; hulls are point sets, [] for all but roofs.
//   polygon: {"outline": [[x, y], ...], "holes": [[[x, y], ...], ...]}
// Slab, stair and roof objects: SM_<floor>_Slab (plynth_kind "slab", plynth_id the floor),
// SM_<floor>_<stair>_Stair ("stair", the stair id), SM_<floor>_Roof_<NN> ("roof", the floor id, NN the index
// from 00, custom properties plynth_roof_index and plynth_roof_section, the entry id or ""), each in its floor's
// collection. Their neutral surfaces, when the material is null: slab edges and tops Wall_Outside, well sides
// Wall_Inside, undersides Soffit; stairs Stair; roof covering Roof (pitched) or Flat_Roof (flat), underside
// Soffit, edge Fascia, gable and parapet Wall_Outside, glazing Glass.
// preview_folder (null when fbx_file is null): after export_fbx, the step preview_parts writes one FBX per
// floor, one for the slab above it and one for its roofs into build/preview/, with the same profile, materials
// and images as building.fbx, and parts.json listing them (BuildPreview.h), so the 3D view can hide roofs, cut
// the building at a floor and the plan overlay shows one floor.
// The fields mean what the structs in BuildGeometry.h and Collision.h say; positions are world coordinates.
// Every material field is the id of an entry of "materials", or null: the surface then gets its neutral look
// (the material is unset, missing from the library or broken, and core has reported it). Materials follow
// Materials.h. Every slot field names an entry of "slots" (MaterialSlots.h); the Blender material of the face
// is named after the slot, and the slot's material is always the same as the material field beside it.
// The worker makes an empty material for every name of "slots" before any asset is appended, so a furniture
// material of the same name keeps its own data under a name with a number suffix and is never written over.
// "surface" in a slot is the neutral surface whose look the slot gets when its material is null (Wall_Inside,
// Wall_Outside, Wall_Top, Reveal, Threshold, Floor, Ceiling, Door, Frame, Glass, Handle, Stair, Roof, Flat_Roof,
// Soffit, Fascia, Generated). Generated is the look of a generator's material left empty.
// "collision" lists the pieces and hulls of floorCollision; the build turns each into a convex mesh
// UCX_<target object name>_<NN> (the pieces, then the hulls), and adds one convex hull per item and per
// generator item (UCX_<item object name>_00, so UCX_SM_<generator>_<item>_00 for a generator's). The target is
// the object with that plynth_kind and plynth_id, and for a roof with that plynth_roof_index.
//
// Output in the result, schema/blender-result.schema.json "buildOutput":
//   {"blend_file": "building.blend", "fbx_file": "building.fbx", "materials_file": "materials.json" or null,
//    "objects": 42, "triangles": 18200,
//    "counts": {"wall_solids": 1, "openings_cut": 9, "fills": 7, "rooms": 5, "items": 4, "materials": 6,
//               "collision": 31, "slabs": 2, "stairs": 1, "roofs": 3, "generators": 2},
//               (generators only when the job has generator items)
//    "steps": [{"step": "walls", "ms": 120}, ...],
//    "problems": [{"code": "cut_failed", "severity": "error", "message": "...", "floor": "f1",
//                  "object": "o3", "step": "openings"}]}
// The worker writes building.blend, building.fbx and materials.json under a temporary name and renames them at
// the end, so a failed build leaves the files of the last good build in place. materials.json
// (schema/build-materials.schema.json) is written with the FBX and says, for every material of the built
// surfaces, which image goes into which channel, because the FBX cannot carry metallic or opacity maps to the
// 3D preview (owner, milestone 8). The neutral looks of surfaces without a material are written as constant
// images into build/textures/.
//
// Preview textures (owner, milestone 10): building.fbx is only for the app's 3D preview, so before it is
// written the export_fbx step points every image larger than preview_texture_size on a side at a scaled copy
// in build/preview_textures/ (named <material or asset>_<channel>, one copy per source file), and
// materials.json lists the same files the FBX uses. building.blend is saved before that and keeps the full
// size images, which is what the export reads. materials.json also lists every asset material, with "asset"
// set to the asset id, so metal and see-through parts of furniture show in the preview.
//
// Collision (step "collision", order 110): one closed convex mesh per piece, hull, item and generator item, plynth_kind
// "collision", custom property plynth_collision_for holding the target object's name, in a collection named
// "Collision". building.blend keeps them; building.fbx and materials.json leave them out, so the 3D preview
// never shows them; the export (ExportJob.h) writes them.
//
// Surfaces and UVs (the "materials" step and the steps that make meshes):
// - a built face's Blender material is named after its slot (MaterialSlots.h); slots that show the same
//   catalog material use the same image files, which is fine for the 3D preview because their looks are the
//   same; furniture keeps the materials of its asset
// - UVs are in metres divided by the material's tile size (neutral surfaces: 1 m): wall side faces u along
//   their ring from its first point and v up from the floor's level; wall tops, floors and thresholds world
//   (X, Y); ceilings (X, -Y); the faces a cut makes, fills and anything else are projected on the world plane
//   their normal is closest to

inline constexpr std::string_view kBuildFolderName = "build";
inline constexpr std::string_view kBuildBlendFileName = "building.blend";
inline constexpr std::string_view kBuildFbxFileName = "building.fbx";
inline constexpr std::string_view kBuildMaterialsFileName = "materials.json";
inline constexpr std::string_view kBuildTexturesFolder = "textures";
inline constexpr std::string_view kBuildPreviewTexturesFolder = "preview_textures";

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

// Problem codes found by core before Blender starts (step "check"):
//   opening_does_not_fit (error, the opening is not cut; the message is checkOpeningFit's),
//   room_not_closed (error, no floor or ceiling), missing_asset (error, the item is left out),
//   asset_broken (error, the asset is Broken or its asset.blend is gone; the item is left out),
//   no_library (error, no library is set or it cannot open; every item is left out, object empty),
//   and every other itemProblemName (warnings: no_room, outside_room, overlaps_item, overlaps_wall,
//   overlaps_opening, off_wall, too_tall, size_changed),
//   missing_material (warning, the surface gets its neutral look), material_broken (warning, the same).
//   One material problem per material id and object that names it: object is the wall, room or opening
//   (ResolvedMaterial::from), or empty for a project default. no_library is also reported when the plan uses a
//   material and there is no library.
// Codes the worker steps report: cut_failed (error), fill_failed (warning, the opening is cut but empty),
// asset_load_failed (error), bevel_failed (warning, the walls are left sharp), material_failed (warning, a
// map could not be loaded and the surface got its neutral look), collision_failed (warning, a collision piece
// or a hull could not be made; object is the room, floor, stair or item). New steps may add codes.
// Also found by core: the StairProblem codes of Stairs.h (object the stair) and the RoofProblem codes
// of Roofs.h (object the roof section entry, or empty). Found by the worker: slab_failed, stair_failed and
// roof_failed (errors: the mesh could not be made and is left out, so the export's build_complete check
// fails; floor is always set; object is a plan id, never an object name: the floor for a slab, the stair for
// a stair, and for a roof its section entry, or the floor when the section follows the plan's default),
// preview_failed (warning: a preview part could not be written; the 3D view then loads building.fbx).
// Generator items (GeneratorBuild.h): found by core missing_generator, generator_broken (errors),
// generator_unavailable, generator_changed (warnings); found by the worker generator_failed (error),
// generator_unavailable, generator_size (warnings). object is the item. generator_unavailable from runBuild,
// when the add-ons could not be checked or copied, has no floor and no object. A generator's material that is
// missing or broken gives missing_material or material_broken with object the item.

// The maps of one material inside an asset, absolute paths (AssetInfo.h). opacity and occlusion are empty
// when it has none. Full size; the build makes its own preview copies.
struct BuildAssetMaterial {
    std::string name; // the material's name in asset.blend
    std::filesystem::path baseColor;
    std::filesystem::path normal;
    std::filesystem::path roughness;
    std::filesystem::path metallic;
    std::filesystem::path opacity;
    std::filesystem::path occlusion;

    bool operator==(const BuildAssetMaterial&) const = default;
};

// What the build needs to know about one asset from the library.
struct BuildAssetState {
    std::filesystem::path blendFile; // items/<id>/asset.blend
    bool broken = false;             // AssetState::Broken, or asset.blend is not there
    std::string problem;             // why, when broken
    std::vector<BuildAssetMaterial> materials; // AssetInfo::materialMaps; empty before the asset is refreshed
};

// Says what the library has for an asset id. Empty when the asset is not in the library.
using BuildAssetSource = std::function<std::optional<BuildAssetState>(std::string_view assetId)>;

// An asset file the job lists. Its objects are in the collection named after the id (AssetInfo.h).
struct BuildAsset {
    std::string id;
    std::filesystem::path blendFile;
    std::vector<BuildAssetMaterial> materials;

    bool operator==(const BuildAsset&) const = default;
};

// A library material the job lists, with its map files. opacity and occlusion are empty when it has none.
struct BuildMaterial {
    std::string id;
    TileSize tileSize;
    std::filesystem::path baseColor;
    std::filesystem::path normal;
    std::filesystem::path roughness;
    std::filesystem::path metallic;
    std::filesystem::path opacity;
    std::filesystem::path occlusion;

    bool operator==(const BuildMaterial&) const = default;
};

// What the build needs to know about one material from the library.
struct BuildMaterialState {
    BuildMaterial material;
    bool broken = false; // MaterialState::Broken, info.json unreadable, or a map file is not there
    std::string problem; // why, when broken
};

// Says what the library has for a material id. Empty when the material is not in the library.
using BuildMaterialSource = std::function<std::optional<BuildMaterialState>(std::string_view materialId)>;

// One material slot of the build (MaterialSlots.h).
struct BuildSlot {
    std::string name;     // for example "Living_Floor"
    std::string material; // the catalog id it shows; empty for the neutral look
    std::string surface;  // the neutral surface whose look it gets when material is empty, for example "Floor"

    bool operator==(const BuildSlot&) const = default;
};

// The material of the wall faces of one wall that look into one room (room empty: outside). material is empty
// for the neutral look. slot is the name of its BuildSlot.
struct WallFaceMaterial {
    Id wall;
    Id room;
    std::string material;
    std::string slot;

    bool operator==(const WallFaceMaterial&) const = default;
};

// The materials of one opening that is built; empty strings get the neutral look. The *Slot fields name the
// BuildSlot of each part.
struct OpeningMaterialIds {
    Id opening;
    std::string reveal;
    std::string threshold;
    std::string door;
    std::string frame;
    std::string glass;
    std::string handle;
    std::string revealSlot;
    std::string thresholdSlot;
    std::string doorSlot;
    std::string frameSlot;
    std::string glassSlot;
    std::string handleSlot;

    bool operator==(const OpeningMaterialIds&) const = default;
};

// The materials of one room that is built; empty strings get the neutral look.
struct RoomMaterialIds {
    Id room;
    std::string floor;
    std::string ceiling;
    std::string floorSlot;
    std::string ceilingSlot;

    bool operator==(const RoomMaterialIds&) const = default;
};

// The material of one stair that is built; empty for the neutral look.
struct StairMaterialId {
    Id stair;
    std::string material;
    std::string slot; // the name of its BuildSlot

    bool operator==(const StairMaterialId&) const = default;
};

// The materials of one roof section, per part (RoofPart); empty strings get the neutral look.
struct RoofMaterialIds {
    int index = 0; // RoofBuild::index
    std::string covering;
    std::string underside;
    std::string edge;
    std::string gable;
    std::string parapet;
    std::string glazing;
    // The BuildSlot of each part.
    std::string coveringSlot;
    std::string undersideSlot;
    std::string edgeSlot;
    std::string gableSlot;
    std::string parapetSlot;
    std::string glazingSlot;

    bool operator==(const RoofMaterialIds&) const = default;
};

// The materials of a floor's slab; empty strings get the neutral look.
struct SlabMaterialIds {
    std::string edge;
    std::string well;
    std::string underside;
    // The BuildSlot of each part.
    std::string edgeSlot; // ledge tops too
    std::string wellSlot;
    std::string undersideSlot;

    bool operator==(const SlabMaterialIds&) const = default;
};

// Every material choice on one floor, after the library check.
struct FloorMaterials {
    Id floor;
    std::vector<WallFaceMaterial> wallFaces;   // one per (wall, room) pair on the floor's ring edges, sorted
    std::string wallTop;
    std::vector<OpeningMaterialIds> openings; // in the order of FloorBuild::openings
    std::vector<RoomMaterialIds> rooms;       // in the order of FloorBuild::rooms
    SlabMaterialIds slab;
    std::vector<StairMaterialId> stairs;      // in the order of FloorBuild::stairs
    std::vector<RoofMaterialIds> roofs;       // in the order of FloorBuild::roofs
    std::string wallTopSlot;

    bool operator==(const FloorMaterials&) const = default;
};

// Everything worked out before Blender starts.
struct BuildPlan {
    std::vector<FloorBuild> floors;   // in plan order
    std::vector<BuildAsset> assets;   // each asset an item that is built uses, once, sorted by id
    std::vector<BuildMaterial> materials;     // each usable material the surfaces use, once, sorted by id
    std::vector<FloorMaterials> floorMaterials; // one per entry of floors, in the same order
    // In floor order, then walls, openings, rooms, items, each in plan order; then the material problems,
    // sorted by material id and object.
    std::vector<BuildProblem> problems;
    // Every slot a built surface uses, once, sorted by name. (Not called "slots": Qt defines that word as a
    // macro, and the app includes this header.)
    std::vector<BuildSlot> materialSlots;
    std::vector<std::vector<CollisionShape>> collision; // floorCollision of each entry of floors, same order
    // The generator items, which are never asset items, with their material slots filled in. Their materials
    // are checked and their slots named like those of the surfaces. problems also holds their problems, each
    // where its item stands in the floor's item order and before that item's other warnings. Without generators
    // the library counts as empty, so no item is dropped silently. Without an assets source there is no library:
    // no_library and no items of either kind.
    GeneratorBuildPlan generators;

    bool operator==(const BuildPlan&) const = default;
};

// Works out the build plan of a project: floorElevations, then for every floor wallSolids, openingBuild for
// each opening (one that does not fit gives opening_does_not_fit), roomBuild for each room (room_not_closed),
// and the items whose asset the source has and is not broken (missing_asset, asset_broken), with itemProblems
// for the rest as warnings. An empty assets source means there is no library: no_library and no items.
// Each floor also gets its stairs (stairLayout; stairProblems, a stair with an error is left out and cuts no
// hole), its slab (slabBuild) and its roofs (floorRoofs), and room floors and ceilings get the stair holes.
// FloorBuild::slabBelow is set on every floor but the lowest, before floorCollision runs.
// Problems of a floor are in the order walls, openings, rooms, items, stairs, roofs.
// Then the materials of every built surface by Materials.h, each checked with the materials source
// (missing_material, material_broken; such a surface gets the neutral look). An empty materials source means
// there is no library: every surface gets the neutral look, and no_library is reported once in all when the
// plan uses a material. Then the slot of every surface by MaterialSlots.h, after the library check (so a
// missing material counts as the neutral look when names are compared), and floorCollision of every floor.
BuildPlan makeBuildPlan(const Project& project, const BuildAssetSource& assets,
                        const BuildMaterialSource& materials = {});

// The same with the library's generators, and the add-on cache when it matches the build's Blender.
BuildPlan makeBuildPlan(const Project& project, const BuildAssetSource& assets, const BuildMaterialSource& materials,
                        const GeneratorLibrary* generators, const std::optional<AddonCache>& addons);

// A problem in its file form: {"code", "severity", "message", "floor", "object", "step"}, with null for an
// empty floor or object. The worker's result, build.json and "plynth build --check" all use it.
Json buildProblemToJson(const BuildProblem& problem);

// Says whether a plan has anything to build. InvalidValue "The plan has no walls to build." when no floor
// has a wall.
Status checkBuildable(const Plan& plan);

// Makes the build job: the envelope of makeBlenderJob with task kBuildTask and the params above. outputFolder
// is projectFolder / kBuildFolderName. fbx_file, materials_file and preview_folder are null when
// settings.writeFbx is false.
// Map paths are absolute, UTF-8, forward slashes.
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
    int materials = 0; // Blender materials made for built surfaces, library and neutral: one per slot
    int slabs = 0;     // slab meshes made
    int stairs = 0;    // stair meshes made
    int roofs = 0;     // roof section meshes made
    int collision = 0;  // UCX_ collision meshes made: pieces, hulls, item and generator item hulls
    int generators = 0; // items built by generators; 0 when the result or report has no count

    bool operator==(const BuildCounts&) const = default;
};

// The build task's result, read and checked. BadFile when a field is missing (counts.collision, slabs, stairs
// and roofs included) or has the wrong type, or a severity is unknown. An older build report without those
// counts reads with them 0. counts.generators may be missing and reads as 0; BadFile when it is there and is
// not a count.
struct BuildWorkerOutput {
    BlenderResultHeader header;
    std::string blendFile;  // file name inside the output folder
    std::string fbxFile;    // file name inside the output folder, empty when none was asked for
    std::string materialsFile; // kBuildMaterialsFileName when written with the FBX, otherwise empty
    long long objects = 0;
    long long triangles = 0;
    BuildCounts counts;
    std::vector<BuildStepTime> steps;
    std::vector<BuildProblem> problems;
};

BuildWorkerOutput readBuildResult(const Json& result, std::string_view jobId);

}
