#pragma once

#include "Status.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace plynth {

// The finishing touches of a build (DESIGN.md sections 7.13a and 7.15): trim, light markers and the variation
// seed. Lengths in metres.

// The kinds of trim the build adds. Names in files: "skirting", "cornice", "casing", "sill", "railing".
enum class TrimKind { Skirting, Cornice, Casing, Sill, Railing };

// The kinds of light marker. Names in files: "ceiling", "wall", "standing".
enum class LightKind { Ceiling, Wall, Standing };

// The kinds of light strip (plan format 8, LightStrips.h). Names in files: "cove", "soffit", "edge", "step".
// cove: a ceiling cove, the strip faces up; soffit: under an overhang or canopy, it faces down; edge: along a slab
// or deck edge, it faces outward; step: under the nose of every step of a stair.
enum class LightStripKind { Cove, Soffit, Edge, Step };

// How one kind of trim is made. profile names an entry of the trim profile registry (TrimProfiles.h). height is
// the size of the profile up the wall (across the wall face for a casing), depth how far it stands out from it.
struct TrimStyle {
    bool enabled = true;
    std::string profile;
    double height = 0.0;
    double depth = 0.0;

    bool operator==(const TrimStyle&) const = default;
};

// The trim of a project. railingHeight is the height of the top of a stair railing above the steps.
struct TrimSettings {
    TrimStyle skirting{true, "classic", 0.10, 0.015};
    TrimStyle cornice{true, "cove", 0.08, 0.08};
    TrimStyle casing{true, "bevel", 0.07, 0.018};
    TrimStyle sill{true, "rounded", 0.03, 0.04};
    TrimStyle railing{true, "round", 0.05, 0.05};
    double railingHeight = 0.9;

    bool operator==(const TrimSettings&) const = default;
};

// The seed of a project's variation and what it may change. Seed 0 builds the plan exactly as marked.
struct VariationSettings {
    std::uint32_t seed = 0;
    bool furniture = true;
    bool materials = true;

    bool operator==(const VariationSettings&) const = default;
};

// The range checkTrimSettings accepts.
inline constexpr double kMinTrimSize = 0.005;
inline constexpr double kMaxTrimSize = 0.5;
inline constexpr double kMinRailingHeight = 0.5;
inline constexpr double kMaxRailingHeight = 1.5;

// The style of one kind of trim.
TrimStyle& trimStyle(TrimSettings& settings, TrimKind kind);
const TrimStyle& trimStyle(const TrimSettings& settings, TrimKind kind);

// Checks every style: height and depth from kMinTrimSize to kMaxTrimSize, the railing height from
// kMinRailingHeight to kMaxRailingHeight, and the profile a well formed lower case name (letters a to z,
// digits and "_", starting with a letter, at most 64). Whether the profile is registered is not checked, so a
// project from another machine still loads. InvalidValue naming the kind.
Status checkTrimSettings(const TrimSettings& settings);

// The name used in files for a trim kind, and the kind for a name. Empty for an unknown name.
std::string_view trimKindName(TrimKind kind);
std::optional<TrimKind> trimKindFromName(std::string_view name);

// The name used in files for a light kind, and the kind for a name. Empty for an unknown name.
std::string_view lightKindName(LightKind kind);
std::optional<LightKind> lightKindFromName(std::string_view name);

// The name used in files for a light strip kind, and the kind for a name. Empty for an unknown name.
std::string_view lightStripKindName(LightStripKind kind);
std::optional<LightStripKind> lightStripKindFromName(std::string_view name);

// The height above the floor a new light marker of a kind starts at: the ceiling height minus 0.1 for a
// ceiling light, 1.8 for a wall light and 1.4 for a standing lamp, never above the ceiling height minus 0.1 and
// never below 0.
double defaultLightHeight(LightKind kind, double ceilingHeight);

}
