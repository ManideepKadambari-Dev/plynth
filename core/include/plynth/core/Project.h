#pragma once

#include "MaterialDefaults.h"
#include "Plan.h"
#include "Status.h"

#include <optional>
#include <string>
#include <string_view>

namespace plynth {

inline constexpr std::string_view kProjectFileName = "project.json";
inline constexpr std::string_view kPlanFileName = "plan.json";
inline constexpr std::string_view kPlansFolderName = "plans";
inline constexpr std::string_view kAutosaveFolderName = "autosave";

inline constexpr double kDefaultGridSize = 0.5;

// The built-in default sizes (DESIGN.md section 7.5). A new project starts with these.
inline constexpr double kDefaultDoorWidth = 0.9;
inline constexpr double kDefaultDoorHeight = 2.1;
inline constexpr double kDefaultWindowWidth = 1.2;
inline constexpr double kDefaultWindowHeight = 1.2;
inline constexpr double kDefaultWindowSill = 0.9;
inline constexpr double kDefaultWallItemCentre = 1.2; // a TV's centre above the floor

// Single gives one FBX for the whole building, Grouped gives one FBX per group.
enum class ExportMode { Single, Grouped };

// The sizes new marks get, per project. Plain openings use the door width and height with no sill.
// ceilingHeight is for floors made later (milestone 12); existing floors keep their own ceiling height.
// wallItemCentre is how high above the floor the centre of a newly mounted wall item goes (DESIGN.md
// section 7.5, "TV centred at 1.2 m").
struct DefaultSizes {
    double ceilingHeight = kDefaultCeilingHeight;
    double doorWidth = kDefaultDoorWidth;
    double doorHeight = kDefaultDoorHeight;
    double windowWidth = kDefaultWindowWidth;
    double windowHeight = kDefaultWindowHeight;
    double windowSill = kDefaultWindowSill;
    double wallItemCentre = kDefaultWallItemCentre;

    bool operator==(const DefaultSizes&) const = default;
};

// Checks default sizes. InvalidValue when a number is not finite, a size is not above zero, the sill is
// below zero, a door or a window (sill plus height) would be taller than the ceiling height, or the wall
// item centre is not above zero and below the ceiling height.
Status checkDefaultSizes(const DefaultSizes& sizes);

// Project wide settings. Saved as project.json.
struct ProjectSettings {
    std::string name = "Untitled";
    ExportMode exportMode = ExportMode::Single;
    double gridSize = kDefaultGridSize;
    DefaultSizes defaults;
    MaterialDefaults materials; // project.json "materials" (format 4)

    bool operator==(const ProjectSettings&) const = default;
};

// Everything that makes up a project in memory: its settings and its plan.
// Commands change this, so settings changes can be undone like plan changes.
struct Project {
    ProjectSettings settings;
    Plan plan;

    bool operator==(const Project&) const = default;
};

// Makes an untitled project with default settings and an empty plan (see makeEmptyPlan).
Project makeNewProject();

// The name used in files for an export mode: "single" or "grouped".
std::string_view exportModeName(ExportMode mode);

// The export mode for a name used in files. Empty when the name is unknown.
std::optional<ExportMode> exportModeFromName(std::string_view name);

}
