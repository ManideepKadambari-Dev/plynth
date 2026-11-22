#pragma once

#include "Status.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace plynth {

// The kinds of surface the build gives a material (DESIGN.md sections 7.2, 7.11 and 7.13a). Each has a
// project default in project.json "materials", used wherever the plan names no material for it.
enum class Surface {
    WallInside,  // "wall_inside": wall faces that look into a room
    WallOutside, // "wall_outside": wall faces that look into no room
    WallTop,     // "wall_top": the tops and bottoms of wall solids
    Floor,       // "floor"
    Ceiling,     // "ceiling"
    Door,        // "door": the leaf of a door fill
    Frame,       // "frame": door and window frames
    Glass,       // "glass": window panes
    Handle,      // "handle": door handles
};

// Every surface, in the order above.
std::span<const Surface> allSurfaces();

// The name used in files and the CLI, for example "wall_inside".
std::string_view surfaceName(Surface surface);

// The surface for a name. Empty when the name is unknown.
std::optional<Surface> surfaceFromName(std::string_view name);

// The material ids of the sample materials in samples/materials/, which new projects use by default.
inline constexpr std::string_view kSampleWhitePaint = "white_paint";
inline constexpr std::string_view kSampleRedBrick = "red_brick";
inline constexpr std::string_view kSampleOakPlanks = "oak_planks";
inline constexpr std::string_view kSamplePaintedWood = "painted_wood";
inline constexpr std::string_view kSampleClearGlass = "clear_glass";
inline constexpr std::string_view kSampleBrushedSteel = "brushed_steel";

// The material each surface gets when the plan names none. Empty means no material: the build gives that
// surface its neutral look. A new project, and a project file from before version 4, starts with the
// sample materials shown here.
struct MaterialDefaults {
    std::optional<std::string> wallInside = std::string(kSampleWhitePaint);
    std::optional<std::string> wallOutside = std::string(kSampleRedBrick);
    std::optional<std::string> wallTop = std::string(kSampleWhitePaint);
    std::optional<std::string> floor = std::string(kSampleOakPlanks);
    std::optional<std::string> ceiling = std::string(kSampleWhitePaint);
    std::optional<std::string> door = std::string(kSamplePaintedWood);
    std::optional<std::string> frame = std::string(kSampleWhitePaint);
    std::optional<std::string> glass = std::string(kSampleClearGlass);
    std::optional<std::string> handle = std::string(kSampleBrushedSteel);

    bool operator==(const MaterialDefaults&) const = default;
};

// The default for one surface.
const std::optional<std::string>& materialDefault(const MaterialDefaults& defaults, Surface surface);

// Changes the default for one surface.
void setMaterialDefault(MaterialDefaults& defaults, Surface surface, std::optional<std::string> material);

// Checks every default that is set is a well formed name (the plan's name rule: letters, digits, "_" and
// "-", at most 128 characters). InvalidValue naming the surface.
Status checkMaterialDefaults(const MaterialDefaults& defaults);

}
