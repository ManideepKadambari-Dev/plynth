#include "plynth/core/MaterialSlots.h"

#include "plynth/core/BuildParts.h"
#include "plynth/core/Materials.h"

#include <string>
#include <utility>

namespace plynth {

namespace {

bool isLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// The plain slot name and the neutral surface of a project surface when a build part shows it.
std::pair<std::string_view, std::string_view> partSurfaceSlot(Surface surface)
{
    switch (surface) {
    case Surface::WallInside: return {"Room_Wall", "Wall_Inside"};
    case Surface::WallOutside: return {kExteriorWallSlot, "Wall_Outside"};
    case Surface::WallTop: return {kWallTopSlot, "Wall_Top"};
    case Surface::Floor: return {"Room_Floor", "Floor"};
    case Surface::Ceiling: return {"Room_Ceiling", "Ceiling"};
    case Surface::Door: return {kDoorSlot, "Door"};
    case Surface::Frame: return {kFrameSlot, "Frame"};
    case Surface::Glass: return {kGlassSlot, "Glass"};
    case Surface::Handle: return {kHandleSlot, "Handle"};
    case Surface::Roof: return {kRoofSlot, "Roof"};
    case Surface::FlatRoof: return {kFlatRoofSlot, "Flat_Roof"};
    case Surface::Stair: return {kStairSlot, "Stair"};
    case Surface::Trim: return {kTrimSlot, "Trim"};
    case Surface::Railing: return {kRailingSlot, "Railing"};
    case Surface::Deck: return {"Deck", "Deck"};
    case Surface::Foundation: return {"Foundation", "Foundation"};
    case Surface::Ground: return {"Ground", "Ground"};
    case Surface::Paving: return {"Paving", "Paving"};
    case Surface::Fence: return {"Fence", "Fence"};
    case Surface::BoundaryWall: return {"Boundary_Wall", "Boundary_Wall"};
    }
    return {kExteriorWallSlot, "Wall_Outside"};
}

}

// Turns an id into PascalCase.
std::string slotPascalCase(std::string_view id, std::string_view fallback)
{
    std::string out;
    bool startOfPart = true;
    for (const char c : id) {
        if (c == '_' || c == '-' || c == ' ') {
            startOfPart = true;
            continue;
        }
        if (!isLetter(c) && !isDigit(c)) continue;
        if (startOfPart && c >= 'a' && c <= 'z') {
            out += static_cast<char>(c - 'a' + 'A');
        } else {
            out += c;
        }
        startOfPart = false;
    }
    if (out.empty()) return std::string(fallback);
    if (isDigit(out.front())) return std::string(fallback) + out;
    return out;
}

// The label of a room's slots.
std::string roomSlotLabel(const Room& room)
{
    if (!room.type) return std::string(kNoRoomTypeLabel);
    return slotPascalCase(*room.type, kNoRoomTypeLabel);
}

// The name of a slot whose plain name shows more than one material.
std::string slotNameWithMaterial(std::string_view base, std::string_view material)
{
    std::string suffix = slotPascalCase(material, "");
    if (suffix.empty()) suffix = std::string(kNeutralSlotSuffix);
    return std::string(base) + "_" + suffix;
}

// The slot key of a part surface that shows a project surface.
PartSlot partSlot(const Project& project, Surface surface, const std::optional<std::string>& material)
{
    const auto [name, look] = partSurfaceSlot(surface);
    std::optional<std::string> chosen = material;
    if (!chosen) chosen = surfaceDefaultMaterial(project, surface).material;
    return partSlot(name, look, chosen);
}

// The slot key of a part surface with its own plain name and neutral surface.
PartSlot partSlot(std::string_view name, std::string_view look, const std::optional<std::string>& material)
{
    PartSlot out;
    out.material = material.value_or(std::string());
    out.slot = std::string(name) + "|" + std::string(look) + "|" + out.material;
    return out;
}

}
