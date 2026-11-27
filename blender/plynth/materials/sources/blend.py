import os
from collections import namedtuple

import bpy

from .. import IGNORE_ALPHA, ChannelPlan, Loaded, SourceEntry
from ...jobs import WorkerError
from ...registry import MATERIAL_SOURCES

SOCKETS = {"base_color": "Base Color", "roughness": "Roughness", "metallic": "Metallic", "opacity": "Alpha",
           "normal": "Normal"}
COLOR_SPACE = {"base_color": "sRGB", "roughness": "Non-Color", "metallic": "Non-Color", "opacity": "Non-Color"}
SHADER_MIXERS = ("ShaderNodeMixShader", "ShaderNodeAddShader")
# Other shaders read as a Principled BSDF: the shader input behind each Principled input, and fixed values.
OTHER_SHADERS = {
    "ShaderNodeBsdfDiffuse": ({"Base Color": "Color", "Normal": "Normal"}, {"Roughness": 1.0, "Metallic": 0.0}),
    "ShaderNodeBsdfGlossy": ({"Base Color": "Color", "Roughness": "Roughness", "Normal": "Normal"}, {"Metallic": 1.0}),
    "ShaderNodeBsdfAnisotropic": ({"Base Color": "Color", "Roughness": "Roughness", "Normal": "Normal"},
                                  {"Metallic": 1.0}),
    "ShaderNodeEmission": ({"Base Color": "Color"}, {"Roughness": 1.0, "Metallic": 0.0}),
    "ShaderNodeBsdfGlass": ({"Base Color": "Color", "Roughness": "Roughness", "Normal": "Normal", "IOR": "IOR"},
                            {"Metallic": 0.0}),
    "ShaderNodeBsdfRefraction": ({"Base Color": "Color", "Roughness": "Roughness", "Normal": "Normal", "IOR": "IOR"},
                                 {"Metallic": 0.0}),
}
GLASS_SHADERS = ("ShaderNodeBsdfGlass", "ShaderNodeBsdfRefraction")
TRANSPARENT_SHADER = "ShaderNodeBsdfTransparent"
# The name of the Principled transmission input: Blender 4 and later, then older versions.
TRANSMISSION_NAMES = ("Transmission Weight", "Transmission")
GROUP_SOCKET_TYPES = {"Base Color": "NodeSocketColor", "Normal": "NodeSocketVector"}
READ_NODE = "Plynth read shader"
MADE_LABEL = "plynth_made"
MAX_DEPTH = 8
# How much light a material must let through to count as glass, and the IOR used when none is given.
GLASS_LEVEL = 0.5
DEFAULT_IOR = 1.45

# How a glass material lets light through: its transmission (None when a texture sets it) and its IOR.
Glass = namedtuple("Glass", "transmission ior")


# Returns the sRGB value of a linear value from 0 to 1.
def to_srgb(value):
    value = max(0.0, min(1.0, value))
    return value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055


# Returns the transmission input of a Principled BSDF, or None when this Blender has none.
def transmission_input(node):
    return next((node.inputs[name] for name in TRANSMISSION_NAMES if name in node.inputs), None)


# Returns the names of the Principled inputs a made Principled BSDF takes over: the channels, the transmission
# and the IOR.
def read_names(node):
    names = list(SOCKETS.values())
    weight = transmission_input(node)
    if weight is not None:
        names.append(weight.name)
    if "IOR" in node.inputs:
        names.append("IOR")
    return names


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


# Returns a Principled BSDF of the tree to read the channels from, or None. It is the one that feeds the output's
# surface through mix and add shaders, or one made for reading from what feeds it: a node group with a Principled
# BSDF inside, a mix of two shaders that can be read, or a diffuse, glossy or emission shader. A made one is
# added to the tree and found again on the next call.
def readable_principled(tree):
    if tree is None:
        return None
    made = tree.nodes.get(READ_NODE)
    if made is not None:
        return made
    output = output_node(tree)
    if output is None:
        return None
    found = shader_principled(tree, output.inputs["Surface"], 0)
    if found is not None and found.label == MADE_LABEL:
        found.name = READ_NODE
    return found


# Returns a Principled BSDF that stands for the shader linked to a socket of the tree, or None.
def shader_principled(tree, socket, depth):
    found = resolve_link(socket) if depth <= MAX_DEPTH else None
    if found is None:
        return None
    node, out = found
    kind = node.bl_idname
    if kind == "ShaderNodeBsdfPrincipled":
        return node
    if kind == "ShaderNodeMixShader":
        first = shader_principled(tree, node.inputs[1], depth + 1)
        second = shader_principled(tree, node.inputs[2], depth + 1)
        if first is not None and second is not None and first != second:
            return mixed_principled(tree, node.inputs[0], first, second)
        if first is None and second is not None and is_transparent(node.inputs[1]):
            return cut_out_principled(tree, node.inputs[0], second, True)
        if second is None and first is not None and is_transparent(node.inputs[2]):
            return cut_out_principled(tree, node.inputs[0], first, False)
        return first or second
    if kind == "ShaderNodeAddShader":
        return shader_principled(tree, node.inputs[0], depth + 1) or shader_principled(tree, node.inputs[1], depth + 1)
    if kind == "ShaderNodeGroup" and node.node_tree is not None:
        return group_principled(tree, node, out, depth)
    if kind in OTHER_SHADERS:
        return converted_principled(tree, node)
    return None


# Returns how a material lets light through as Glass, or None when it is not glass. It is glass when a Glass or
# Refraction shader feeds its surface, or a Principled BSDF whose transmission is a texture or at least GLASS_LEVEL.
def glass_of(tree):
    output = output_node(tree) if tree is not None else None
    if output is None:
        return None
    return glass_behind(output.inputs["Surface"], 0)


# Returns the Glass of the first glass shader behind a socket, looking through mix and add shaders and node groups.
def glass_behind(socket, depth):
    found = resolve_link(socket) if depth <= MAX_DEPTH else None
    if found is None:
        return None
    node, out = found
    kind = node.bl_idname
    if kind in GLASS_SHADERS:
        return Glass(1.0, ior_of(node))
    if kind == "ShaderNodeBsdfPrincipled":
        weight = transmission_input(node)
        if weight is None:
            return None
        if resolve_link(weight) is not None:
            return Glass(None, ior_of(node))
        return Glass(float(weight.default_value), ior_of(node)) if weight.default_value >= GLASS_LEVEL else None
    if kind in SHADER_MIXERS:
        start = 1 if kind == "ShaderNodeMixShader" else 0
        for entry in list(node.inputs)[start:]:
            glass = glass_behind(entry, depth + 1)
            if glass is not None:
                return glass
        return None
    if kind == "ShaderNodeGroup" and node.node_tree is not None:
        exit_node = group_output(node.node_tree)
        entry = None
        if exit_node is not None:
            entry = next((s for s in exit_node.inputs if s.identifier == out.identifier), None)
        return glass_behind(entry, depth + 1) if entry is not None else None
    return None


# Returns the IOR a shader sets, or DEFAULT_IOR when it has none or a texture sets it.
def ior_of(node):
    if "IOR" not in node.inputs or node.inputs["IOR"].is_linked:
        return DEFAULT_IOR
    return float(node.inputs["IOR"].default_value)


# Adds a Principled BSDF that is only read, never rendered.
def new_principled(tree):
    node = tree.nodes.new("ShaderNodeBsdfPrincipled")
    node.label = MADE_LABEL
    return node


# Returns the value of an input as a list of numbers.
def as_list(socket):
    value = socket.default_value
    return list(value) if hasattr(value, "__len__") else [value]


# Makes the target input take what the source input has: the same link, or the same value.
def copy_input(tree, source, target):
    found = resolve_link(source)
    if found is not None:
        tree.links.new(found[1], target)
        return
    values = as_list(source)
    size = len(as_list(target))
    if size == 1:
        target.default_value = sum(values[:3]) / len(values[:3])
    elif len(values) == 1:
        target.default_value = values * size
    else:
        target.default_value = (values + [1.0] * size)[:size]


# Returns the socket of a node with this identifier.
def by_identifier(sockets, identifier):
    return next(s for s in sockets if s.identifier == identifier)


# Makes a Principled BSDF whose inputs mix those of two Principled BSDFs by the mix shader's factor. Inputs
# that are equal values are copied, and the normal is not mixed: the first linked one is used.
def mixed_principled(tree, factor, first, second):
    made = new_principled(tree)
    for name in read_names(made):
        a, b, target = first.inputs[name], second.inputs[name], made.inputs[name]
        same = not a.is_linked and not b.is_linked and as_list(a) == as_list(b)
        if name in ("Normal", "IOR") or same:
            copy_input(tree, a if a.is_linked or not b.is_linked else b, target)
            continue
        mix = tree.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA" if name == "Base Color" else "FLOAT"
        kind = "Color" if name == "Base Color" else "Float"
        copy_input(tree, factor, by_identifier(mix.inputs, "Factor_Float"))
        copy_input(tree, a, by_identifier(mix.inputs, "A_" + kind))
        copy_input(tree, b, by_identifier(mix.inputs, "B_" + kind))
        tree.links.new(by_identifier(mix.outputs, "Result_" + kind), target)
    return made


# Makes a Principled BSDF from a diffuse, glossy or emission shader.
def converted_principled(tree, node):
    links, values = OTHER_SHADERS[node.bl_idname]
    made = new_principled(tree)
    for name, source in links.items():
        if source in node.inputs:
            copy_input(tree, node.inputs[source], made.inputs[name])
    for name, value in values.items():
        made.inputs[name].default_value = value
    weight = transmission_input(made)
    if node.bl_idname in GLASS_SHADERS and weight is not None:
        weight.default_value = 1.0
    return made


# Says whether a socket is fed by a Transparent BSDF.
def is_transparent(socket):
    found = resolve_link(socket)
    return found is not None and found[0].bl_idname == TRANSPARENT_SHADER


# Makes a Principled BSDF for a mix of a Transparent BSDF and a readable shader, the usual cut-out of leaves:
# it has the shader's inputs, and its Alpha is the share of the mix that shows the shader, times the shader's
# own Alpha. transparent_first says the Transparent BSDF is the mix's first shader, shown where the factor is 0.
def cut_out_principled(tree, factor, shader, transparent_first):
    made = new_principled(tree)
    for name in read_names(made):
        if name != "Alpha":
            copy_input(tree, shader.inputs[name], made.inputs[name])
    alpha = shader.inputs["Alpha"]
    opaque = not alpha.is_linked and alpha.default_value >= 1.0
    if transparent_first and opaque:
        copy_input(tree, factor, made.inputs["Alpha"])
        return made
    share = tree.nodes.new("ShaderNodeMath")
    if transparent_first:
        share.operation = "ADD"
        copy_input(tree, factor, share.inputs[0])
        share.inputs[1].default_value = 0.0
    else:
        share.operation = "SUBTRACT"
        share.inputs[0].default_value = 1.0
        copy_input(tree, factor, share.inputs[1])
    if opaque:
        tree.links.new(share.outputs[0], made.inputs["Alpha"])
        return made
    both = tree.nodes.new("ShaderNodeMath")
    both.operation = "MULTIPLY"
    tree.links.new(share.outputs[0], both.inputs[0])
    copy_input(tree, alpha, both.inputs[1])
    tree.links.new(both.outputs[0], made.inputs["Alpha"])
    return made


# Returns the group output node that counts: the active one, else any.
def group_output(tree):
    outputs = [n for n in tree.nodes if n.bl_idname == "NodeGroupOutput"]
    return next((n for n in outputs if n.is_active_output), outputs[0] if outputs else None)


# Makes a Principled BSDF for a node group that holds one. The group gets a copy of its tree, so other
# materials keep theirs, with an extra output for each linked input of the inner Principled BSDF; the made
# Principled BSDF reads those outputs and the inner one's values.
def group_principled(tree, node, out, depth):
    inner_tree = node.node_tree.copy()
    node.node_tree = inner_tree
    exit_node = group_output(inner_tree)
    if exit_node is None:
        return None
    entry = next((s for s in exit_node.inputs if s.identifier == out.identifier), None)
    inner = shader_principled(inner_tree, entry, depth + 1) if entry is not None else None
    if inner is None:
        return None
    made = new_principled(tree)
    for name in read_names(made):
        if name not in inner.inputs:
            continue
        source = inner.inputs[name]
        found = resolve_link(source)
        if found is None:
            copy_input(tree, source, made.inputs[name])
            continue
        item = inner_tree.interface.new_socket(name="Plynth " + name, in_out="OUTPUT",
                                               socket_type=GROUP_SOCKET_TYPES.get(name, "NodeSocketFloat"))
        inner_tree.links.new(found[1], by_identifier(exit_node.inputs, item.identifier))
        tree.links.new(by_identifier(node.outputs, item.identifier), made.inputs[name])
    return made


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
        ignore = channel == "opacity" and kind == "file" and node.image.alpha_mode in ("CHANNEL_PACKED", "NONE")
        return ChannelPlan(channel, kind, path, None, IGNORE_ALPHA if ignore else None)
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
    principled = readable_principled(material.node_tree)
    if principled is None:
        raise WorkerError("no_base_color", "The material %s has no Principled BSDF or other shader Plynth can read, "
                                           "in its nodes or its node groups." % wanted)
    blend_folder = os.path.dirname(os.path.abspath(path))
    plans = {channel: plan_for(ctx, channel, principled.inputs[name], material.name, blend_folder)
             for channel, name in SOCKETS.items()}
    return Loaded(plans, wanted, None, None)


MATERIAL_SOURCES.register("blend", SourceEntry(("blend",), load))
