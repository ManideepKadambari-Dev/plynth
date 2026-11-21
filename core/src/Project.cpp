#include "plynth/core/Project.h"

#include <cmath>
#include <cstdio>

namespace plynth {

namespace {

// Writes a length in metres with two decimals for a message.
std::string metres(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.2f m", value);
    return text;
}

// Says whether a number is finite and above zero.
bool above0(double v)
{
    return std::isfinite(v) && v > 0.0;
}

}

// Checks default sizes against the rules in Project.h.
Status checkDefaultSizes(const DefaultSizes& sizes)
{
    const auto bad = [](const char* what, const char* rule) {
        return fail(ErrorCode::InvalidValue, std::string("The ") + what + " must be " + rule + ".");
    };
    if (!above0(sizes.ceilingHeight)) return bad("ceiling height", "a number above zero");
    if (!above0(sizes.doorWidth)) return bad("door width", "a number above zero");
    if (!above0(sizes.doorHeight)) return bad("door height", "a number above zero");
    if (!above0(sizes.windowWidth)) return bad("window width", "a number above zero");
    if (!above0(sizes.windowHeight)) return bad("window height", "a number above zero");
    if (!std::isfinite(sizes.windowSill) || sizes.windowSill < 0.0) return bad("window sill", "zero or more");
    if (!above0(sizes.wallItemCentre)) return bad("wall item centre", "a number above zero");
    if (sizes.wallItemCentre >= sizes.ceilingHeight) {
        return fail(ErrorCode::InvalidValue,
                    "The wall item centre " + metres(sizes.wallItemCentre) + " must be below the ceiling height "
                        + metres(sizes.ceilingHeight) + ".");
    }
    if (sizes.doorHeight > sizes.ceilingHeight) {
        return fail(ErrorCode::InvalidValue,
                    "The door height " + metres(sizes.doorHeight) + " is more than the ceiling height "
                        + metres(sizes.ceilingHeight) + ".");
    }
    if (sizes.windowSill + sizes.windowHeight > sizes.ceilingHeight) {
        return fail(ErrorCode::InvalidValue,
                    "The window sill " + metres(sizes.windowSill) + " plus the window height " + metres(sizes.windowHeight)
                        + " is more than the ceiling height " + metres(sizes.ceilingHeight) + ".");
    }
    return {};
}

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
