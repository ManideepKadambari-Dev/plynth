#pragma once

#include "Plan.h"

#include <optional>
#include <string>
#include <string_view>

namespace plynth {

// How bad a build problem is.
enum class BuildSeverity {
    Error,   // "error": the object was left out of the build or is incomplete
    Warning, // "warning": it was built, but should be checked
};

// "error" or "warning".
std::string_view buildSeverityName(BuildSeverity severity);

// The severity for a name used in files. Empty when the name is unknown.
std::optional<BuildSeverity> buildSeverityFromName(std::string_view name);

// The step of problems core finds before Blender starts.
inline constexpr std::string_view kCheckStep = "check";

// One problem of a build. BuildJob.h lists the codes.
struct BuildProblem {
    std::string code;
    BuildSeverity severity = BuildSeverity::Error;
    std::string message; // one plain sentence, for example "Door o3 does not fit its wall: it reaches 0.1 m past the corner."
    Id floor;            // empty when it is not about one floor
    Id object;           // the plan id the app marks: a point, wall, room, opening or item; empty for none
    std::string step;    // kCheckStep, or the worker step that found it

    bool operator==(const BuildProblem&) const = default;
};

}
