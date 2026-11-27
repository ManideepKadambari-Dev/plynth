CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")

# For each channel: the static switch that turns its map on, the texture parameter and the texture output read.
MAP_PARAMETERS = {
    "base_color": ("UseBaseColorMap", "BaseColorMap", "RGB"),
    "normal": ("UseNormalMap", "NormalMap", "RGB"),
    "roughness": ("UseRoughnessMap", "RoughnessMap", "R"),
    "metallic": ("UseMetallicMap", "MetallicMap", "R"),
    "opacity": ("UseOpacityMap", "OpacityMap", "R"),
    "occlusion": ("UseOcclusionMap", "OcclusionMap", "R"),
}

# The parameter used for a channel that has no map.
VALUE_PARAMETERS = {"base_color": "BaseColor", "roughness": "Roughness", "metallic": "Metallic",
                    "opacity": "Opacity"}

# The parent material for each blend.
PARENTS = {"opaque": "M_Plynth_Opaque", "masked": "M_Plynth_Masked", "translucent": "M_Plynth_Translucent"}

# The channels each parent reads; opaque has no opacity.
PARENT_CHANNELS = {
    "opaque": ("base_color", "normal", "roughness", "metallic", "occlusion"),
    "masked": CHANNELS,
    "translucent": CHANNELS,
}

# The textures the parents' samplers show until an instance sets its own: name, color, channel, sRGB.
DEFAULT_TEXTURES = {
    "color": ("T_Plynth_Default_Color", (255, 255, 255), "base_color", True),
    "normal": ("T_Plynth_Default_Normal", (128, 128, 255), "normal", False),
    "mask": ("T_Plynth_Default_Mask", (255, 255, 255), "roughness", False),
}


# Returns which default texture a channel's sampler starts with.
def default_for(channel):
    if channel == "base_color":
        return "color"
    if channel == "normal":
        return "normal"
    return "mask"
