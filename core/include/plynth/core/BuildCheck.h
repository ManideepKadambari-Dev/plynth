#pragma once

#include "BuildProblem.h"
#include "BuildResolve.h"
#include "Library.h"
#include "Project.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Checking a build without running Blender, and previewing a variation without building (issues 41 and 50).
// The app's "Check build" action and the Variation group of the Properties panel use the same calls as
// "plynth build --check" and "plynth variation preview", so both list the same problems and choices.

// What a check found.
struct BuildCheck {
    Status status;                     // InvalidValue from checkBuildable when there is nothing to build
    std::vector<BuildProblem> problems; // makeBuildPlan's, in its order
    int errors = 0;
    int warnings = 0;
    std::string planFingerprint;
    bool libraryOpened = false;        // false: no library, so every item is reported as no_library
};

// Opens the library's catalogs when a library is given and they open (a failure counts as no library), resolves
// the build with the library's choices (resolveBuild), and makes the build plan with the library's assets,
// materials and generators and no add-on cache. Runs no Blender and writes nothing.
BuildCheck checkBuild(const Project& project, const std::optional<LibraryPaths>& library);

// The "plynth build --check" result: {"problems": [buildProblem], "error_count", "warning_count",
// "plan_fingerprint"}.
Json buildCheckToJson(const BuildCheck& check);

// What a seed would change.
struct VariationPreview {
    ResolvedBuild resolved;
    std::optional<LibraryPaths> library; // the library the choices came from; empty for none
};

// Resolves the project with the library's choices, with seed in place of the project's seed when given. A
// library that does not open gives no choices.
VariationPreview previewVariation(const Project& project, std::optional<std::uint32_t> seed,
                                  const std::optional<LibraryPaths>& library);

// The "plynth variation preview" result: the variation settings used, "library", "changes" [{"target",
// "object", "from", "to"}], "frames" [{"opening", "asset"}], "rooms" [{"floor", "room", "dressing", "props":
// [{"asset", "rule", "host", "x", "y", "z", "rotation"}]}] and "prop_count".
Json variationPreviewToJson(const VariationPreview& preview);

}
