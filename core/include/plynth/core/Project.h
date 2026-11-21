#pragma once

#include "Plan.h"

#include <optional>
#include <string>
#include <string_view>

namespace plynth {

inline constexpr std::string_view kProjectFileName = "project.json";
inline constexpr std::string_view kPlanFileName = "plan.json";
inline constexpr std::string_view kPlansFolderName = "plans";
inline constexpr std::string_view kAutosaveFolderName = "autosave";

inline constexpr double kDefaultGridSize = 0.5;

// Single gives one FBX for the whole building, Grouped gives one FBX per group.
enum class ExportMode { Single, Grouped };

// Project wide settings. Saved as project.json.
struct ProjectSettings {
    std::string name = "Untitled";
    ExportMode exportMode = ExportMode::Single;
    double gridSize = kDefaultGridSize;

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
