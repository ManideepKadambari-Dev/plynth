#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace plynth {

// The image maps of a surface material, shared by catalog materials (MaterialInfo.h) and the materials inside
// assets (AssetInfo.h). The conventions are the ones at the top of MaterialInfo.h: every channel the 3D preview
// and Unreal read is an image, base color sRGB, the rest linear and grey maps read from the red channel,
// normals in the OpenGL convention.

enum class MapChannel { BaseColor, Normal, Roughness, Metallic, Opacity, Occlusion };

// Every channel in the order above.
std::span<const MapChannel> allMapChannels();

// "base_color", "normal", "roughness", "metallic", "opacity", "occlusion", and back. Empty for an unknown name.
std::string_view mapChannelName(MapChannel channel);
std::optional<MapChannel> mapChannelFromName(std::string_view name);

// How a map was made.
enum class MapOrigin {
    File,  // "file": from an image of the source; detail is its file name
    Value, // "value": a constant image from a single value; detail is the value, for example "0.85"
    Baked, // "baked": rendered from a Blender material's nodes with Eevee
    None,  // "none": there is no map (opacity of an opaque material, a missing occlusion)
};

std::string_view mapOriginName(MapOrigin origin);
std::optional<MapOrigin> mapOriginFromName(std::string_view name);

struct MaterialMap {
    std::string file; // relative to the item folder with forward slashes, "textures/normal.png";
                      // empty when origin is None
    MapOrigin origin = MapOrigin::None;
    std::string detail;

    bool operator==(const MaterialMap&) const = default;
};

struct MaterialMaps {
    MaterialMap baseColor;
    MaterialMap normal;
    MaterialMap roughness;
    MaterialMap metallic;
    MaterialMap opacity;
    MaterialMap occlusion;

    bool operator==(const MaterialMaps&) const = default;
};

const MaterialMap& materialMap(const MaterialMaps& maps, MapChannel channel);
MaterialMap& materialMap(MaterialMaps& maps, MapChannel channel);

}
