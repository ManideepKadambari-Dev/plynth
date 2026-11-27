#pragma once

#include "Addons.h"
#include "BuildGeometry.h"
#include "BuildProblem.h"
#include "Generators.h"
#include "Plan.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The generator part of the build (DESIGN.md section 7.13 step 7). Core works out which generator items are
// built, where they go and with which values; the worker step "generators" (order 70,
// blender/plynth/building/generators.py) runs each generator file and places what it makes.
//
// Job params, schema/blender-job.schema.json, build task:
//   "generators": [{"id": "column", "file": "D:/Library/generators/column.py", "addons": []}]
//     each generator a built item uses, once, sorted by id
//   every floor: "generator_items": [{"id": "i7", "generator": "column",
//     "params": {"diameter": 0.3, "height": 3.0, "shape": "round", "material": "white_paint"},
//     "location": [x, y, z], "rotation_z": r, "size": {"width", "depth", "height"}, "room": "r1", "wall": null,
//     "slots": {"material": "Column_Material"}}]
//     location and rotation_z as for items (itemPlacement in BuildGeometry.h); material values are the id of an
//     entry of the job's materials list or null for the neutral look kGeneratedSurface
//     slots has one entry per material parameter, the name of an entry of the job's slots list (MaterialSlots.h),
//     and is {} when materialSlots is empty; gen.material(param) gives the material of that name; faces with any
//     other material get the slot of the first declared material parameter, or kGeneratedSlot when there is none
// Worker problem codes: generator_failed (error, the item is left out: the file could not be loaded, has no
// generate function, raised an error, or made no mesh), generator_unavailable (warning, an add-on it needs could
// not be enabled in the build's Blender; the item is left out), generator_size (warning, what it made reaches
// more than kGeneratorSizeTolerance past its size on some side). The output counts gain "generators": the items
// built by generators.
//
// What the worker does with each item, so every generator script can stay small: it calls generate(gen) in an
// empty scene collection, keeps the mesh objects generate returns (curves, text and geometry node results made
// real first, modifiers applied), joins them into one mesh named <generator>_<item> with its pivot at the
// item's location (generators make their result around the origin: bottom centre at (0, 0, 0), front toward -Y,
// as catalog assets), turns it by rotation_z, gives it plynth_id (the item), plynth_kind "generator" and
// plynth_generator (the id), and removes every other object and data block the generator made.

inline constexpr std::string_view kGeneratorsStep = "generators";
inline constexpr std::string_view kGeneratedSurface = "Generated"; // the neutral look of a material left empty
inline constexpr double kGeneratorSizeTolerance = 0.01; // metres

// Problem codes core finds in the check step for generator items:
//   missing_generator (error: not in the library; the item is left out),
//   generator_broken (error: its file is in the library folder but was refused; the message is the refusal),
//   generator_unavailable (warning: the add-on cache for the build's Blender says an add-on it needs is off; the
//   item is left out),
//   generator_changed (warning: its stored values do not fit the current declaration; the build uses
//   fitGeneratorParams, and the message lists the notes).
inline constexpr std::string_view kMissingGeneratorProblem = "missing_generator";
inline constexpr std::string_view kGeneratorBrokenProblem = "generator_broken";
inline constexpr std::string_view kGeneratorUnavailableProblem = "generator_unavailable";
inline constexpr std::string_view kGeneratorChangedProblem = "generator_changed";

// A generator file the job lists.
struct BuildGenerator {
    std::string id;
    std::filesystem::path file;
    std::vector<std::string> addons;

    bool operator==(const BuildGenerator&) const = default;
};

// The slot of one material parameter of a generator item.
struct GeneratorMaterialSlot {
    std::string param;
    std::string material; // the catalog id after the library check; empty for the neutral look
    std::string slot;     // the name of its BuildSlot

    bool operator==(const GeneratorMaterialSlot&) const = default;
};

// One generator item as the build makes it.
struct GeneratorItemBuild {
    Id id;
    std::string generator;
    std::vector<GeneratorParamValue> params;  // after fitGeneratorParams, sorted by id
    std::vector<std::string> materialParams;  // the ids of params that are materials
    Vec3 location;
    double rotationZ = 0.0;
    ItemSize size;                            // generatorSize of params
    std::optional<Id> room;
    std::optional<Id> wall;
    std::vector<GeneratorMaterialSlot> materialSlots; // in materialParams order; filled by makeBuildPlan

    bool operator==(const GeneratorItemBuild&) const = default;
};

struct GeneratorFloorBuild {
    Id floor;
    std::vector<GeneratorItemBuild> items; // in the floor's item order

    bool operator==(const GeneratorFloorBuild&) const = default;
};

// Everything about generators worked out before Blender starts.
struct GeneratorBuildPlan {
    std::vector<BuildGenerator> generators;    // each generator a built item uses, once, sorted by id
    std::vector<GeneratorFloorBuild> floors;   // one per plan floor, in plan order
    std::vector<std::string> materials;        // every non empty material value of the built items, sorted, once
    bool needsAddons = false;                  // a built item's generator needs an add-on
    std::vector<BuildProblem> problems;        // in floor order, then item order; step kCheckStep

    bool operator==(const GeneratorBuildPlan&) const = default;
};

// Works out the generator part of a build. Floor levels come from floorElevations and places from itemPlacement.
// Asset items are skipped. With no library, nothing is built and nothing is reported (the build reports
// no_library for every item). addons is the cache, passed only when it matches the build's Blender; without it
// no item is left out for its add-ons and the worker decides.
GeneratorBuildPlan makeGeneratorBuildPlan(const Plan& plan, const GeneratorLibrary* library,
                                          const std::optional<AddonCache>& addons);

// Says whether a material id can be used by the build (it is in the job's materials list).
using MaterialUsable = std::function<bool(std::string_view materialId)>;

// The job's "generators" list, as above.
Json generatorJobList(const GeneratorBuildPlan& plan);

// One floor's "generator_items" list, as above. A material value that is empty or not usable is null. An empty
// usable function takes every material as usable.
Json generatorJobItems(const GeneratorFloorBuild& floor, const MaterialUsable& usable = {});

}
