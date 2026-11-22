import os

import numpy as np

from .. import (KEEP_SUFFIXES, BakerEntry, copy_file, map_entry, read_pixels, read_size, to_bytes, write_grey,
                write_png)
from ... import shading
from ...registry import BAKERS

ORDER = 10
FLAT_NORMAL = (128, 128, 255)


# Returns the path a map is written to in the textures folder.
def target(textures, channel, suffix):
    return os.path.join(textures, channel + suffix)


# Copies a png or jpg as it is after checking it reads, or rewrites any other image as png. An opacity map
# that is all 1 gives None.
def from_file(plan, textures):
    suffix = os.path.splitext(plan.file)[1].lower()
    name = os.path.basename(plan.file)
    if plan.channel == "opacity" and np.all(read_pixels(plan.file)[:, :, 0] >= 0.999):
        return None
    if suffix in KEEP_SUFFIXES:
        read_size(plan.file)
        copy_file(plan.file, target(textures, plan.channel, suffix))
        return map_entry(plan.channel, "file", name, suffix)
    pixels = read_pixels(plan.file)
    write_png(target(textures, plan.channel, ".png"), to_bytes(pixels[:, :, :3]))
    return map_entry(plan.channel, "file", name)


# Writes a DirectX normal map with its green channel flipped, so it reads as OpenGL.
def from_directx(plan, textures):
    rgb = to_bytes(read_pixels(plan.file)[:, :, :3])
    rgb[:, :, 1] = 255 - rgb[:, :, 1]
    write_png(target(textures, plan.channel, ".png"), rgb)
    return map_entry(plan.channel, "file", os.path.basename(plan.file))


# Writes roughness as one minus a glossiness map.
def from_gloss(plan, textures):
    plane = to_bytes(read_pixels(plan.file)[:, :, 0])
    write_grey(target(textures, plan.channel, ".png"), (255 - plane) / 255.0)
    return map_entry(plan.channel, "file", os.path.basename(plan.file))


# Writes the grey map held in one channel of a packed image (occlusion, roughness, metallic).
def from_packed(plan, textures):
    plane = read_pixels(plan.file)[:, :, plan.node]
    write_grey(target(textures, plan.channel, ".png"), plane)
    return map_entry(plan.channel, "file", os.path.basename(plan.file))


# Writes opacity from the alpha channel of the base color image. None when nothing is see-through.
def from_alpha(plan, textures):
    alpha = read_pixels(plan.file)[:, :, 3]
    if np.all(alpha >= 0.999):
        return None
    write_grey(target(textures, plan.channel, ".png"), alpha)
    return map_entry(plan.channel, "file", os.path.basename(plan.file))


# Writes a small constant image for a single value. Opacity and occlusion of 1 give None.
def from_value(plan, textures):
    path = target(textures, plan.channel, ".png")
    if plan.channel == "normal":
        shading.write_constant_png(path, FLAT_NORMAL)
        return map_entry(plan.channel, "value", "flat")
    if plan.channel in ("opacity", "occlusion") and plan.value >= 1.0:
        return None
    grey = shading.to_byte(plan.value)
    shading.write_constant_png(path, (grey, grey, grey))
    return map_entry(plan.channel, "value", "%g" % plan.value)


# Writes a small constant image of an sRGB color.
def from_color(plan, textures):
    rgb = tuple(shading.to_byte(c) for c in plan.value)
    shading.write_constant_png(target(textures, plan.channel, ".png"), rgb)
    return map_entry(plan.channel, "value", ", ".join("%g" % c for c in plan.value))


HANDLERS = {
    "file": from_file,
    "directx": from_directx,
    "gloss": from_gloss,
    "packed": from_packed,
    "alpha": from_alpha,
    "value": from_value,
    "color": from_color,
}


# Says whether this baker can make the channel: anything that is an image or a single value.
def can_bake(ctx, plan):
    return plan.kind in HANDLERS


# Makes the map of one channel into the textures folder. Returns its entry, or None when there is no map.
def bake(ctx, plan, textures):
    return HANDLERS[plan.kind](plan, textures)


BAKERS.register("direct", BakerEntry(ORDER, can_bake, bake))
