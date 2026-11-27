#pragma once

#include "Project.h"
#include "Status.h"

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <span>
#include <string_view>

namespace plynth {

// ordered_json keeps keys in the order they were written, so saved files read top to bottom
// in the same order as schema/plan.schema.json.
using Json = nlohmann::ordered_json;

// Version 2 of plan.json adds "hinge" and "swing" to openings, version 3 adds "items" to floors, version 4
// adds "wall_material" to rooms, version 5 adds "slab_thickness", "image_offset" and "stairs" to floors and
// "roof" and "roof_sections" to the plan, version 6 adds "generator" to items (and lets "asset" be null for a
// generator item), version 7 adds "lights" to floors, "dressing" to rooms and "auto_frame" to openings,
// version 8 adds "cladding" to walls, "fill" to openings, "on" to items, "rise", "cheeks" and "railing" to
// stairs, "second_pitch", "break_ratio", "ridge_offset" and "rise" to roof settings, "decks", "railings",
// "canopies", "dormers" and "light_strips" to floors, and "ground_floor", "site" and "foundation" to the plan.
// Version 2 of project.json adds "defaults", version 3 adds "wall_item_centre" to them, version 4 adds
// "materials" (MaterialDefaults.h), version 5 adds "roof", "flat_roof" and "stair" to "materials", version 6
// adds "trim" and "variation" (FinishSettings.h) and "trim" and "railing" to "materials", version 7 adds "deck",
// "foundation", "ground", "paving", "fence" and "boundary_wall" to "materials".
// The plan is written as version 8 and the project as version 7 once the steps below are in; every older version
// stays readable for ever (docs/COMPATIBILITY.md), with a saved file of each kept under tests/data/formats/.
// The schema for the current version is schema/plan.schema.json and schema/project.schema.json; older
// versions stay as schema/plan.vN.schema.json and schema/project.vN.schema.json.
inline constexpr std::string_view kPlanFormatName = "plynth-plan";
inline constexpr int kPlanFormatVersion = 8;
inline constexpr std::string_view kProjectFormatName = "plynth-project";
inline constexpr int kProjectFormatVersion = 7;

// Turns a plan into its plan.json form. Every field is written, absent values as null.
Json planToJson(const Plan& plan);

// The answer from reading a plan: the plan, or an error saying what is wrong and where.
struct PlanRead {
    Status status;
    Plan plan;
    int fileVersion = 0;   // the version found in the file, before any upgrade
    bool migrated = false; // true when upgrade steps ran
};

// Reads a plan from its plan.json form. Checks "format", upgrades older versions with
// planMigrations(), refuses newer versions, reads every field (missing optional fields get
// their defaults, unknown fields are refused) and finally runs checkPlan.
// A newer version is UnsupportedVersion with kNewerFileMessage, naming the file's version.
PlanRead planFromJson(const Json& json);

// The sentence for a project file written by a newer Plynth. %1 is the file name and %2 its version.
inline constexpr std::string_view kNewerFileMessage =
    "%1 was saved by a newer Plynth (format version %2). Update Plynth to open this project.";

// Turns project settings into their project.json form.
Json settingsToJson(const ProjectSettings& settings);

// The answer from reading project settings.
struct SettingsRead {
    Status status;
    ProjectSettings settings;
    int fileVersion = 0;
    bool migrated = false;
};

// Reads project settings from their project.json form, with the same checks as planFromJson.
SettingsRead settingsFromJson(const Json& json);

// Upgrades a file's JSON from one version to the next, in place.
// The step does not need to change "version"; migrateJson does that after it succeeds.
using MigrationStep = std::function<Status(Json& json)>;

// One upgrade step and the version it starts from.
struct Migration {
    int fromVersion = 0;
    MigrationStep step;
};

// Runs upgrade steps until the JSON reaches currentVersion. Reads "version" from the JSON.
// BadFile when "version" is missing, not a whole number, below 1 or a step is missing.
// UnsupportedVersion when it is above currentVersion. A failing step stops and returns its status.
Status migrateJson(Json& json, int currentVersion, std::span<const Migration> steps);

// The upgrade steps for plan.json, ordered by fromVersion.
// 1 to 2: every opening gets "hinge": "start" and "swing": "right" unless it already has them.
// 2 to 3: every floor gets "items": [] unless it already has them.
// 3 to 4: every room gets "wall_material": null unless it already has one.
// 4 to 5: every floor gets "slab_thickness": 0.2, "image_offset": {"x": 0, "y": 0} and "stairs": [], and the
// plan gets "roof" with the RoofSettings defaults and "roof_sections": [], each unless already there.
// 5 to 6: every item gets "generator": null unless it already has one.
// 6 to 7: every floor gets "lights": [], every room "dressing": true and every opening "auto_frame": true, each
// unless already there.
// 7 to 8: every wall gets "cladding": [], every opening "fill": null, every item "on": null, every stair
// "rise": null, "cheeks": false and "railing": true, the roof default and every roof section's settings
// "second_pitch": 15, "break_ratio": 0.5, "ridge_offset": 0 and "rise": 1, every floor "decks", "railings",
// "canopies", "dormers" and "light_strips" as [], and the plan "ground_floor": null, "site" with the ground off
// and no boundaries or surfaces, and "foundation" off with its defaults, each unless already there.
// When the format changes: raise kPlanFormatVersion, add a step here, keep the old schema as
// plan.vN.schema.json and update plan.schema.json.
std::span<const Migration> planMigrations();

// The upgrade steps for project.json.
// 1 to 2: adds "defaults" with the built-in sizes unless it is already there.
// 2 to 3: adds "wall_item_centre": 1.2 to "defaults" unless it is already there.
// 3 to 4: adds "materials" with the built-in MaterialDefaults unless it is already there.
// 4 to 5: adds "roof": "roof_tiles", "flat_roof": "concrete" and "stair": "oak_planks" to "materials", each
// unless already there.
// 5 to 6: adds "trim": "painted_wood" and "railing": "brushed_steel" to "materials", and "trim" and "variation"
// with their defaults, each unless already there.
// 6 to 7: adds "deck": "oak_planks", "foundation": "concrete", "ground": "grass", "paving": "concrete",
// "fence": "painted_wood" and "boundary_wall": "red_brick" to "materials", each unless already there.
std::span<const Migration> projectMigrations();

}
