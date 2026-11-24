#include "plynth/core/MaterialDefaults.h"

#include "commands/CommandSupport.h"

#include <array>

namespace plynth {

namespace {

constexpr std::array<Surface, 14> kSurfaces = {Surface::WallInside, Surface::WallOutside, Surface::WallTop, Surface::Floor,
                                               Surface::Ceiling,    Surface::Door,        Surface::Frame,   Surface::Glass,
                                               Surface::Handle,     Surface::Roof,        Surface::FlatRoof, Surface::Stair,
                                               Surface::Trim,       Surface::Railing};

}

// Every surface, in order.
std::span<const Surface> allSurfaces()
{
    return kSurfaces;
}

// The file name of a surface.
std::string_view surfaceName(Surface surface)
{
    switch (surface) {
    case Surface::WallInside: return "wall_inside";
    case Surface::WallOutside: return "wall_outside";
    case Surface::WallTop: return "wall_top";
    case Surface::Floor: return "floor";
    case Surface::Ceiling: return "ceiling";
    case Surface::Door: return "door";
    case Surface::Frame: return "frame";
    case Surface::Glass: return "glass";
    case Surface::Handle: return "handle";
    case Surface::Roof: return "roof";
    case Surface::FlatRoof: return "flat_roof";
    case Surface::Stair: return "stair";
    case Surface::Trim: return "trim";
    case Surface::Railing: return "railing";
    }
    return "wall_inside";
}

// The surface for a file name.
std::optional<Surface> surfaceFromName(std::string_view name)
{
    for (const Surface surface : kSurfaces) {
        if (surfaceName(surface) == name) return surface;
    }
    return std::nullopt;
}

namespace {

// The field of the defaults that holds a surface.
std::optional<std::string> MaterialDefaults::*field(Surface surface)
{
    switch (surface) {
    case Surface::WallInside: return &MaterialDefaults::wallInside;
    case Surface::WallOutside: return &MaterialDefaults::wallOutside;
    case Surface::WallTop: return &MaterialDefaults::wallTop;
    case Surface::Floor: return &MaterialDefaults::floor;
    case Surface::Ceiling: return &MaterialDefaults::ceiling;
    case Surface::Door: return &MaterialDefaults::door;
    case Surface::Frame: return &MaterialDefaults::frame;
    case Surface::Glass: return &MaterialDefaults::glass;
    case Surface::Handle: return &MaterialDefaults::handle;
    case Surface::Roof: return &MaterialDefaults::roof;
    case Surface::FlatRoof: return &MaterialDefaults::flatRoof;
    case Surface::Stair: return &MaterialDefaults::stair;
    case Surface::Trim: return &MaterialDefaults::trim;
    case Surface::Railing: return &MaterialDefaults::railing;
    }
    return &MaterialDefaults::wallInside;
}

}

// The default for one surface.
const std::optional<std::string>& materialDefault(const MaterialDefaults& defaults, Surface surface)
{
    return defaults.*field(surface);
}

// Changes the default for one surface.
void setMaterialDefault(MaterialDefaults& defaults, Surface surface, std::optional<std::string> material)
{
    defaults.*field(surface) = std::move(material);
}

// Checks every default that is set is a well formed name.
Status checkMaterialDefaults(const MaterialDefaults& defaults)
{
    for (const Surface surface : kSurfaces) {
        const std::optional<std::string>& value = materialDefault(defaults, surface);
        if (value && !detail::isMaterialName(*value)) {
            return fail(ErrorCode::InvalidValue,
                        "The default material for " + std::string(surfaceName(surface)) + " is not a valid name.",
                        "/materials/" + std::string(surfaceName(surface)));
        }
    }
    return {};
}

}
