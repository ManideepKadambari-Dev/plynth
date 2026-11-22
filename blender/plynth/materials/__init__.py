import os
import shutil
import struct
import zlib
from collections import namedtuple

import bpy
import numpy as np

CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
REQUIRED_CHANNELS = ("base_color", "normal", "roughness", "metallic")
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".bmp", ".exr")
KEEP_SUFFIXES = (".png", ".jpg", ".jpeg")
DEFAULT_TILE = 1.0

# What a source says about one channel. kind is file, directx, gloss, packed, alpha, value, color or node;
# file is an image path, value a number or an sRGB triple, node something only the source understands.
ChannelPlan = namedtuple("ChannelPlan", "channel kind file value node")
# A source: the job source kinds it reads and load(ctx, source) -> Loaded.
SourceEntry = namedtuple("SourceEntry", "kinds load")
# A baker: order, can_bake(ctx, plan) -> bool and bake(ctx, plan, folder) -> map or None.
BakerEntry = namedtuple("BakerEntry", "order can_bake bake")
# A check: order and run(ctx, found) which reports through ctx.warn.
CheckEntry = namedtuple("CheckEntry", "order run")
# What a source returns: plans by channel, the picked material, what material.json said and its tile size.
Loaded = namedtuple("Loaded", "plans picked_material source_info tile_size")
# What the checks see: the maps made, their absolute paths, the tile size.
Found = namedtuple("Found", "maps paths tile_size")


# Returns the dictionary of one finished map in the form of the result schema.
def map_entry(channel, origin, detail, suffix=".png"):
    if origin == "none":
        return {"file": None, "from": "none", "detail": ""}
    return {"file": "textures/%s%s" % (channel, suffix), "from": origin, "detail": detail}


# Writes an 8 bit PNG without colour management. data is rows of pixels, shape (height, width, 1, 3 or 4),
# top row first. Each row gets the cheapest of three filters.
def write_png(path, data):
    data = np.ascontiguousarray(data, dtype=np.uint8)
    height, width, channels = data.shape
    flat = data.reshape(height, width * channels).astype(np.int16)
    left = np.zeros_like(flat)
    left[:, channels:] = flat[:, :-channels]
    up = np.zeros_like(flat)
    up[1:] = flat[:-1]
    options = [(flat & 255).astype(np.uint8), ((flat - left) & 255).astype(np.uint8),
               ((flat - up) & 255).astype(np.uint8)]
    costs = [np.abs(option.view(np.int8).astype(np.int32)).sum(axis=1) for option in options]
    best = np.argmin(np.stack(costs), axis=0)
    rows = bytearray()
    for index in range(height):
        rows.append(int(best[index]))
        rows += options[int(best[index])][index].tobytes()
    color_type = {1: 0, 3: 2, 4: 6}[channels]

    def chunk(kind, body):
        block = kind + body
        return struct.pack(">I", len(body)) + block + struct.pack(">I", zlib.crc32(block) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 6))
                     + chunk(b"IEND", b""))


# Loads an image file and returns its pixels as floats from 0 to 1, shape (height, width, 4), top row first.
# The values are the file's own: no colour space is applied.
def read_pixels(path):
    try:
        image = bpy.data.images.load(path, check_existing=False)
    except Exception as error:
        raise ValueError("%s could not be read: %s" % (os.path.basename(path), error))
    try:
        image.colorspace_settings.name = "Non-Color"
        image.alpha_mode = "CHANNEL_PACKED"
        width, height = image.size
        if width == 0 or height == 0:
            raise ValueError("%s has no pixels." % os.path.basename(path))
        buffer = np.empty(width * height * 4, dtype=np.float32)
        image.pixels.foreach_get(buffer)
    finally:
        bpy.data.images.remove(image)
    return np.flipud(buffer.reshape(height, width, 4))


# Returns (width, height) of an image file. Raises ValueError when it cannot be read.
def read_size(path):
    try:
        image = bpy.data.images.load(path, check_existing=False)
    except Exception as error:
        raise ValueError("%s could not be read: %s" % (os.path.basename(path), error))
    try:
        width, height = int(image.size[0]), int(image.size[1])
    finally:
        bpy.data.images.remove(image)
    if width == 0 or height == 0:
        raise ValueError("%s has no readable pixels." % os.path.basename(path))
    return width, height


# Converts floats from 0 to 1 to bytes.
def to_bytes(values):
    return np.clip(np.rint(np.asarray(values) * 255.0), 0, 255).astype(np.uint8)


# Writes a grey map from one 2D array of floats from 0 to 1, as a grey PNG with three equal channels.
def write_grey(path, plane):
    plane = to_bytes(plane)
    write_png(path, np.repeat(plane[:, :, None], 3, axis=2))


# Copies a file unless source and target are the same file.
def copy_file(source, target):
    if os.path.exists(target) and os.path.samefile(source, target):
        return
    shutil.copyfile(source, target)
