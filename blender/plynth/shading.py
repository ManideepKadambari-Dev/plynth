import os
import struct
import zlib

import bpy

from . import surfaces

CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
CONSTANT_SIZE = 4


class MapError(Exception):
    pass


# Writes a small PNG of one flat colour (8 bit RGB, values 0 to 255) without any colour management.
def write_constant_png(path, rgb):
    row = b"\x00" + bytes(rgb) * CONSTANT_SIZE
    raw = row * CONSTANT_SIZE

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", CONSTANT_SIZE, CONSTANT_SIZE, 8, 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(data)


# Converts a 0 to 1 value to a 0 to 255 byte.
def to_byte(value):
    return max(0, min(255, int(round(value * 255.0))))


# Writes the four or five constant images of a neutral look into the folder and returns the map paths by
# channel, as absolute paths with forward slashes. opacity is None when the look is opaque.
def write_neutral_maps(folder, surface, look):
    color, roughness, metallic, alpha = look
    values = {
        "base_color": [to_byte(c) for c in color],
        "normal": [128, 128, 255],
        "roughness": [to_byte(roughness)] * 3,
        "metallic": [to_byte(metallic)] * 3,
    }
    if alpha < 1.0:
        values["opacity"] = [to_byte(alpha)] * 3
    maps = {channel: None for channel in CHANNELS}
    for channel, rgb in values.items():
        path = os.path.abspath(os.path.join(folder, "%s_%s.png" % (surface, channel))).replace("\\", "/")
        write_constant_png(path, rgb)
        maps[channel] = path
    return maps


# Loads an image file with the given colour space. Raises MapError when it is missing or will not decode.
def load_image(path, colorspace):
    if not os.path.isfile(path):
        raise MapError("%s does not exist." % os.path.basename(path))
    try:
        image = bpy.data.images.load(path, check_existing=False)
    except Exception as error:
        raise MapError("%s could not be read: %s" % (os.path.basename(path), error))
    if image.size[0] == 0 or image.size[1] == 0:
        bpy.data.images.remove(image)
        raise MapError("%s has no pixels." % os.path.basename(path))
    image.colorspace_settings.name = colorspace
    return image


# Adds an image texture node holding the image.
def image_node(tree, image, label, x, y):
    node = tree.nodes.new("ShaderNodeTexImage")
    node.image = image
    node.label = label
    node.location = (x, y)
    return node


# Turns a material into a Principled material fed by the image files in maps (channel to path or None).
# base_color is sRGB, the other maps are Non Color; grey maps feed their socket through the Color output; the
# normal map goes through a Normal Map node; opacity feeds Alpha and puts the material in blended mode;
# occlusion stays as a loose labelled node because Principled has no input for it. Every image is loaded
# before the material is touched, so a map that fails leaves it unchanged. Raises MapError.
def apply_maps(mat, maps):
    images = {}
    for channel in CHANNELS:
        path = maps.get(channel)
        if path:
            images[channel] = load_image(path, "sRGB" if channel == "base_color" else "Non-Color")
    if "base_color" not in images:
        raise MapError("the material has no base color image.")
    try:
        mat.use_nodes = True
    except Exception:
        pass
    tree = mat.node_tree
    tree.nodes.clear()
    output = tree.nodes.new("ShaderNodeOutputMaterial")
    output.location = (600, 0)
    shader = tree.nodes.new("ShaderNodeBsdfPrincipled")
    shader.location = (300, 0)
    tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    sockets = {"base_color": "Base Color", "roughness": "Roughness", "metallic": "Metallic", "opacity": "Alpha"}
    y = 300
    for channel, image in images.items():
        node = image_node(tree, image, channel, -300, y)
        y -= 280
        if channel in sockets:
            tree.links.new(node.outputs["Color"], shader.inputs[sockets[channel]])
        elif channel == "normal":
            normal = tree.nodes.new("ShaderNodeNormalMap")
            normal.location = (0, node.location.y)
            tree.links.new(node.outputs["Color"], normal.inputs["Color"])
            tree.links.new(normal.outputs["Normal"], shader.inputs["Normal"])
    set_blend(mat, "opacity" in images)
    return images


# Puts the material in blended mode when blend is true, opaque otherwise.
def set_blend(mat, blend):
    if hasattr(mat, "surface_render_method"):
        mat.surface_render_method = "BLENDED" if blend else "DITHERED"
    elif hasattr(mat, "blend_method"):
        mat.blend_method = "BLEND" if blend else "OPAQUE"


# Gives a material the neutral look of a surface as constant images written into the folder. Returns the maps.
def apply_neutral(mat, surface, folder):
    maps = write_neutral_maps(folder, surface, surfaces.LOOKS[surface])
    apply_maps(mat, maps)
    return maps
