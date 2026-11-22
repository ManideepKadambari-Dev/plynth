import os

import bpy

from .. import ChannelPlan, Loaded, SourceEntry
from ...jobs import WorkerError
from ...registry import MATERIAL_SOURCES

SOCKETS = {"base_color": "Base Color", "roughness": "Roughness", "metallic": "Metallic", "opacity": "Alpha",
           "normal": "Normal"}
COLOR_SPACE = {"base_color": "sRGB", "roughness": "Non-Color", "metallic": "Non-Color", "opacity": "Non-Color"}
SHADER_MIXERS = ("ShaderNodeMixShader", "ShaderNodeAddShader")


# Returns the sRGB value of a linear value from 0 to 1.
def to_srgb(value):
    value = max(0.0, min(1.0, value))
    return value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055


# Returns (node, socket) at the real source of an input, following reroute nodes. None when the input has no
# usable link.
def resolve_link(socket):
    if not socket.is_linked:
        return None
    link = socket.links[0]
    while link.is_valid and not link.is_muted and link.from_node.bl_idname == "NodeReroute":
        entry = link.from_node.inputs[0]
        if not entry.is_linked:
            return None
        link = entry.links[0]
    if not link.is_valid or link.is_muted:
        return None
    return link.from_node, link.from_socket


# Returns the material output node that renders: the active one for all or Eevee, else any.
def output_node(tree):
    outputs = [n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial"]
    for node in outputs:
        if node.is_active_output and node.target in ("ALL", "EEVEE"):
            return node
    return outputs[0] if outputs else None


# Returns the first Principled BSDF that feeds the output's surface, looking through mix and add shaders.
def find_principled(tree):
    output = output_node(tree)
    if output is None:
        return None
    pending = [output.inputs["Surface"]]
    while pending:
        found = resolve_link(pending.pop(0))
        if found is None:
            continue
        node = found[0]
        if node.bl_idname == "ShaderNodeBsdfPrincipled":
            return node
        if node.bl_idname in SHADER_MIXERS:
            pending.extend(s for s in node.inputs if s.type == "SHADER")
    return None


# Returns the absolute path of an image held on disk, or None. A packed image is written to a folder of the
# work area first.
def image_file(ctx, image, blend_folder):
    if image is None or image.source != "FILE":
        return None
    if image.packed_file is not None:
        folder = os.path.join(ctx.work_folder, "blend_images")
        os.makedirs(folder, exist_ok=True)
        ctx.state.setdefault("temp_folders", []).append(folder)
        suffix = ".jpg" if image.file_format == "JPEG" else ".png"
        stem = os.path.splitext(os.path.basename(image.name))[0]
        target = os.path.join(folder, "%s_%d%s" % (stem, abs(hash(image.name)) % 100000, suffix))
        try:
            image.filepath_raw = target
            image.save()
        except RuntimeError:
            return None
        return target
    path = bpy.path.abspath(image.filepath, start=blend_folder)
    return path if os.path.isfile(path) else None


# Returns the image file of an image texture node that is used as it is: no vector input, flat projection,
# repeating, and the colour space the channel needs. None otherwise.
def plain_texture(ctx, node, colorspace, blend_folder):
    if node.bl_idname != "ShaderNodeTexImage" or node.inputs["Vector"].is_linked:
        return None
    if node.projection != "FLAT" or node.extension != "REPEAT" or node.image is None:
        return None
    if colorspace is not None and node.image.colorspace_settings.name != colorspace:
        return None
    return image_file(ctx, node.image, blend_folder)


# Returns the plan for an input that is not linked, or whose link cannot be followed: its own value.
def value_plan(channel, socket):
    if channel == "base_color":
        return ChannelPlan(channel, "color", None, [to_srgb(c) for c in socket.default_value[:3]], None)
    if channel == "normal":
        return ChannelPlan(channel, "value", None, "flat", None)
    return ChannelPlan(channel, "value", None, float(socket.default_value), None)


# Returns the plan for a normal that comes from a tangent space Normal Map node of strength 1 fed by a plain
# image, which is the image itself. None for any other setup.
def direct_normal(ctx, node, blend_folder):
    if node.bl_idname != "ShaderNodeNormalMap" or node.space != "TANGENT":
        return None
    strength = node.inputs["Strength"]
    if strength.is_linked or abs(strength.default_value - 1.0) > 1e-6:
        return None
    source = resolve_link(node.inputs["Color"])
    if source is None or source[1].name != "Color":
        return None
    return plain_texture(ctx, source[0], "Non-Color", blend_folder)


# Says what feeds one Principled input and returns a ChannelPlan: the image itself when it is linked straight
# from a plain image texture, a value when nothing is linked, and a node reference to render otherwise.
def plan_for(ctx, channel, socket, material_name, blend_folder):
    found = resolve_link(socket)
    if found is None:
        return value_plan(channel, socket)
    node, out = found
    path = None
    kind = "file"
    if channel == "normal":
        path = direct_normal(ctx, node, blend_folder)
    elif out.name == "Alpha" and channel == "opacity":
        path = plain_texture(ctx, node, None, blend_folder)
        kind = "alpha"
    elif out.name == "Color":
        path = plain_texture(ctx, node, COLOR_SPACE[channel], blend_folder)
    if path:
        return ChannelPlan(channel, kind, path, None, None)
    reference = {"material": material_name, "node": node.name, "socket": list(node.outputs).index(out)}
    return ChannelPlan(channel, "node", None, None, reference)


# Returns the names of the materials in a .blend file. Nothing is loaded.
def material_names(path):
    with bpy.data.libraries.load(path, link=False) as (source, _):
        return list(source.materials)


# Appends one material with the images and node groups it uses and returns it.
def append_material(path, name):
    with bpy.data.libraries.load(path, link=False) as (source, target):
        target.materials = [name]
    material = target.materials[0]
    material.use_fake_user = True
    return material


# Removes the appended material and the images that came with it.
def forget(material, images):
    bpy.data.materials.remove(material)
    for image in images:
        if image.name in bpy.data.images and image.users <= 1:
            bpy.data.images.remove(image)


# Reads the .blend file without changing it and decides how each channel of the picked material is made.
# Raises import_failed, several_materials and no_base_color.
def load(ctx, source):
    path = source["path"]
    if not os.path.isfile(path) or os.path.splitext(path)[1].lower() != ".blend":
        raise WorkerError("import_failed", "%s is not a .blend file." % os.path.basename(path))
    try:
        names = material_names(path)
    except Exception as error:
        raise WorkerError("import_failed", "%s could not be opened: %s" % (os.path.basename(path), error))
    wanted = source.get("material")
    if wanted is None:
        if not names:
            raise WorkerError("import_failed", "%s holds no material." % os.path.basename(path))
        if len(names) > 1:
            raise WorkerError("several_materials", "The file holds several materials: %s." % ", ".join(sorted(names)))
        wanted = names[0]
    elif wanted not in names:
        raise WorkerError("import_failed", "The file has no material named %s." % wanted)

    before = set(bpy.data.images)
    material = append_material(path, wanted)
    images = [i for i in bpy.data.images if i not in before]
    ctx.state.setdefault("cleanups", []).append(lambda: forget(material, images))
    principled = find_principled(material.node_tree) if material.node_tree else None
    if principled is None:
        raise WorkerError("no_base_color", "The material %s has no Principled BSDF to read." % wanted)
    blend_folder = os.path.dirname(os.path.abspath(path))
    plans = {channel: plan_for(ctx, channel, principled.inputs[name], material.name, blend_folder)
             for channel, name in SOCKETS.items()}
    return Loaded(plans, wanted, None, None)


MATERIAL_SOURCES.register("blend", SourceEntry(("blend",), load))
