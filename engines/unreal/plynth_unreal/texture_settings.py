import unreal

from . import ue

# The compression each channel gets. Data maps share the masks setting, which keeps them linear.
COMPRESSION = {
    "base_color": "TC_DEFAULT",
    "normal": "TC_NORMALMAP",
    "roughness": "TC_MASKS",
    "metallic": "TC_MASKS",
    "opacity": "TC_MASKS",
    "occlusion": "TC_MASKS",
}

# The sampler type a parent material reads each channel's texture with; it must match the compression.
SAMPLER = {
    "base_color": "SAMPLERTYPE_COLOR",
    "normal": "SAMPLERTYPE_NORMAL",
    "roughness": "SAMPLERTYPE_MASKS",
    "metallic": "SAMPLERTYPE_MASKS",
    "opacity": "SAMPLERTYPE_MASKS",
    "occlusion": "SAMPLERTYPE_MASKS",
}


# Sets compression, sRGB and the green channel of a texture for its channel. Plynth's normal maps are OpenGL
# style, so their green channel is flipped for Unreal. Virtual texturing is turned off so the parent
# materials' samplers always match.
def apply(texture, channel, srgb):
    compression = ue.enum_value("TextureCompressionSettings", COMPRESSION[channel])
    if compression is not None:
        ue.try_set(texture, "compression_settings", compression)
    ue.try_set(texture, "srgb", bool(srgb))
    ue.try_set(texture, "flip_green_channel", channel == "normal")
    ue.try_set(texture, "virtual_texture_streaming", False)
    if channel == "normal":
        group = ue.enum_value("TextureGroup", "TEXTUREGROUP_WORLD_NORMAL_MAP")
        if group is not None:
            ue.try_set(texture, "lod_group", group)


# Returns what is wrong with a texture's settings for its channel, or an empty text.
def problem(texture, channel, srgb):
    wanted = ue.enum_value("TextureCompressionSettings", COMPRESSION[channel])
    actual = ue.try_get(texture, "compression_settings")
    if wanted is not None and actual != wanted:
        return "uses %s compression instead of %s" % (actual, COMPRESSION[channel])
    actual_srgb = ue.try_get(texture, "srgb")
    if actual_srgb is not None and bool(actual_srgb) != bool(srgb):
        return "has sRGB %s instead of %s" % ("on" if actual_srgb else "off", "on" if srgb else "off")
    return ""


# Returns the sampler type for a channel.
def sampler(channel):
    return ue.enum_value("MaterialSamplerType", SAMPLER[channel])


# Returns True when the object is a texture.
def is_texture(asset):
    return isinstance(asset, unreal.Texture)
