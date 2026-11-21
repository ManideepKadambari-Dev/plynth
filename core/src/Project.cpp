#include "plynth/core/Project.h"

namespace plynth {

// Makes an untitled project with default settings and an empty plan.
Project makeNewProject()
{
    Project p;
    p.plan = makeEmptyPlan();
    return p;
}

// The name used in files for an export mode.
std::string_view exportModeName(ExportMode mode)
{
    return mode == ExportMode::Grouped ? "grouped" : "single";
}

// The export mode for a name used in files.
std::optional<ExportMode> exportModeFromName(std::string_view name)
{
    if (name == "single") return ExportMode::Single;
    if (name == "grouped") return ExportMode::Grouped;
    return std::nullopt;
}

}
