"""Small .blend models for the import cleanup checks: a glass pane, a leaf card with an alpha map and a model under a
turned empty. The tests make them in a temporary folder. Run with Blender to write them all into a folder:

    blender --background --factory-startup --python import_fixtures.py -- <folder>
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))

from plynth.materials import write_png  # noqa: E402

GLASS_KINDS = ("principled", "glass_shader")
LEAF_KINDS = ("alpha", "mix", "named_uv", "mask", "black")
MODEL_KINDS = ("parent", "keyed", "hidden", "constraint", "nested", "shape_keys")
LEAF_SIZE = 64
LEAF_RADIUS = 24
LEAF_GREEN = (40, 140, 40)
NAMED_UV = "UVChannel_1"
# The model: a box lying along its own Y axis, made upright only by the empty it hangs from.
BOX_SIZE = (0.2, 1.0, 0.3)
MOUNT_PLACE = (1.0, 2.0, 0.5)
UPRIGHT = (math.radians(90.0), 0.0, 0.0)


# Starts an empty scene.
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# Saves the scene as a .blend file at path and starts an empty scene again.
def save(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path, compress=False)
    reset()


# Adds a mesh object made of the given points, faces and UVs (one per face corner) and returns it.
def add_mesh(name, points, faces, uvs=None, uv_name="UVMap"):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(points, [], faces)
    layer = mesh.uv_layers.new(name=uv_name)
    if uvs is not None:
        for index, uv in enumerate(uvs):
            layer.data[index].uv = uv
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# Returns the eight corners and six faces of a box of the given size around a centre.
def box_shape(size, centre=(0.0, 0.0, 0.0)):
    sx, sy, sz = size
    cx, cy, cz = centre
    points = [(cx + px * sx, cy + py * sy, cz + pz * sz) for pz in (-0.5, 0.5) for py in (-0.5, 0.5)
              for px in (-0.5, 0.5)]
    faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    return points, faces


# Adds a box of the given size around a centre and returns it.
def add_box(name, size, centre=(0.0, 0.0, 0.0)):
    points, faces = box_shape(size, centre)
    return add_mesh(name, points, faces)


# Makes a material with a Principled BSDF and returns (material, principled).
def new_material(name):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    principled = next(n for n in material.node_tree.nodes if n.bl_idname == "ShaderNodeBsdfPrincipled")
    return material, principled


# Puts a material in blended mode, as store models with see-through parts are.
def set_blended(material):
    if hasattr(material, "surface_render_method"):
        material.surface_render_method = "BLENDED"
    elif hasattr(material, "blend_method"):
        material.blend_method = "BLEND"


# Returns the transmission input of a Principled BSDF in this Blender.
def transmission_input(principled):
    return next(principled.inputs[n] for n in ("Transmission Weight", "Transmission") if n in principled.inputs)


# Makes the clear glass material of a kind: a Principled BSDF with full transmission, or a Glass BSDF.
def glass_material(kind):
    material, principled = new_material("Glass")
    tree = material.node_tree
    if kind == "principled":
        principled.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
        principled.inputs["Roughness"].default_value = 0.05
        principled.inputs["IOR"].default_value = 1.5
        transmission_input(principled).default_value = 1.0
    else:
        tree.nodes.remove(principled)
        glass = tree.nodes.new("ShaderNodeBsdfGlass")
        glass.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
        glass.inputs["Roughness"].default_value = 0.0
        glass.inputs["IOR"].default_value = 1.5
        output = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        tree.links.new(glass.outputs["BSDF"], output.inputs["Surface"])
    set_blended(material)
    return material


# Writes a window: an upright glass pane one metre square on an opaque wooden sill, and saves it at path.
def make_glass_pane(path, kind="principled"):
    reset()
    pane = add_box("Pane", (1.0, 0.02, 1.0), (0.0, 0.0, 0.58))
    pane.data.materials.append(glass_material(kind))
    sill = add_box("Sill", (1.1, 0.06, 0.08), (0.0, 0.0, 0.04))
    wood, principled = new_material("Frame")
    principled.inputs["Base Color"].default_value = (0.4, 0.2, 0.08, 1.0)
    sill.data.materials.append(wood)
    save(path)


# Returns the leaf shape: True inside a round leaf in the middle of a square image, top row first.
def leaf_shape():
    rows, columns = np.mgrid[0:LEAF_SIZE, 0:LEAF_SIZE]
    middle = (LEAF_SIZE - 1) / 2.0
    return (rows - middle) ** 2 + (columns - middle) ** 2 <= LEAF_RADIUS ** 2


# Writes leaf.png, a green leaf with see-through corners whose hidden pixels are black, as store leaf atlases are.
def write_leaf_png(folder):
    inside = leaf_shape()
    pixels = np.zeros((LEAF_SIZE, LEAF_SIZE, 4), dtype=np.uint8)
    pixels[inside, :3] = LEAF_GREEN
    pixels[inside, 3] = 255
    path = os.path.join(folder, "leaf.png")
    write_png(path, pixels)
    return path


# Writes mask.png, a white image that holds the leaf shape only in its alpha.
def write_mask_png(folder):
    pixels = np.full((LEAF_SIZE, LEAF_SIZE, 4), 255, dtype=np.uint8)
    pixels[:, :, 3] = np.where(leaf_shape(), 255, 0)
    path = os.path.join(folder, "mask.png")
    write_png(path, pixels)
    return path


# Adds an image texture node reading a file in a colour space.
def image_node(tree, path, colorspace):
    image = bpy.data.images.load(path, check_existing=False)
    image.colorspace_settings.name = colorspace
    node = tree.nodes.new("ShaderNodeTexImage")
    node.image = image
    return node


# Makes the leaf material of a kind. alpha: the image's alpha feeds Alpha. mix: a Transparent BSDF mixed with
# the leaf by the image's alpha. named_uv: the image reads a UV map by name. mask: the alpha of a separate white
# mask image feeds Alpha through its colour. black: Alpha comes from a node that gives 0 everywhere.
def leaf_material(kind, folder):
    material, principled = new_material("Leaf")
    tree = material.node_tree
    leaf = image_node(tree, write_leaf_png(folder), "sRGB")
    tree.links.new(leaf.outputs["Color"], principled.inputs["Base Color"])
    if kind == "alpha":
        tree.links.new(leaf.outputs["Alpha"], principled.inputs["Alpha"])
    elif kind == "mix":
        mix = tree.nodes.new("ShaderNodeMixShader")
        transparent = tree.nodes.new("ShaderNodeBsdfTransparent")
        output = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        tree.links.new(leaf.outputs["Alpha"], mix.inputs[0])
        tree.links.new(transparent.outputs[0], mix.inputs[1])
        tree.links.new(principled.outputs["BSDF"], mix.inputs[2])
        tree.links.new(mix.outputs[0], output.inputs["Surface"])
    elif kind == "named_uv":
        uv = tree.nodes.new("ShaderNodeUVMap")
        uv.uv_map = NAMED_UV
        tree.links.new(uv.outputs["UV"], leaf.inputs["Vector"])
        tree.links.new(leaf.outputs["Alpha"], principled.inputs["Alpha"])
    elif kind == "mask":
        mask = image_node(tree, write_mask_png(folder), "Non-Color")
        tree.links.new(mask.outputs["Color"], principled.inputs["Alpha"])
    else:
        zero = tree.nodes.new("ShaderNodeMath")
        zero.operation = "MULTIPLY"
        zero.inputs[0].default_value = 0.0
        zero.inputs[1].default_value = 1.0
        tree.links.new(zero.outputs[0], principled.inputs["Alpha"])
    set_blended(material)
    return material


# Writes a plant card: two upright leaf quads half a metre square that cross, with UVs from 0 to 1, and saves it
# at path with its images beside it.
def make_leaf_card(path, kind="alpha"):
    reset()
    folder = os.path.dirname(path)
    os.makedirs(folder, exist_ok=True)
    half = 0.25
    points = [(-half, 0, 0), (half, 0, 0), (half, 0, 2 * half), (-half, 0, 2 * half),
              (0, -half, 0), (0, half, 0), (0, half, 2 * half), (0, -half, 2 * half)]
    corners = [(0, 0), (1, 0), (1, 1), (0, 1)]
    card = add_mesh("Card", points, [(0, 1, 2, 3), (4, 5, 6, 7)], corners * 2,
                    NAMED_UV if kind == "named_uv" else "UVMap")
    card.data.materials.append(leaf_material(kind, folder))
    save(path)


# Returns the world size (width, depth, height) of the model box under a world matrix.
def box_world_size(world):
    points, _faces = box_shape(BOX_SIZE)
    placed = [world @ Vector(p) for p in points]
    return tuple(max(p[axis] for p in placed) - min(p[axis] for p in placed) for axis in range(3))


# Adds an empty at a place, turn and scale and returns it.
def add_empty(name, location, rotation, scale=(1.0, 1.0, 1.0)):
    empty = bpy.data.objects.new(name, None)
    empty.location = location
    empty.rotation_euler = rotation
    empty.scale = scale
    bpy.context.scene.collection.objects.link(empty)
    return empty


# Writes a box that is upright only through what holds it, saves it at path and returns its world size. parent:
# a turned empty. keyed, hidden, shape_keys: the same with a keyed turn, off in the viewport, or shape keys.
# constraint: a Child Of constraint. nested: an unevenly scaled empty holding a turned one, which slants the box.
def make_rotated_model(path, kind="parent"):
    reset()
    box = add_box("Box", BOX_SIZE)
    mount = add_empty("Mount", MOUNT_PLACE, UPRIGHT)
    world = Matrix.LocRotScale(Vector(MOUNT_PLACE), Euler(UPRIGHT), Vector((1.0, 1.0, 1.0)))
    if kind == "nested":
        mount.scale = (1.0, 2.0, 1.0)
        inner = add_empty("Arm", (0.3, 0.0, 0.0), (0.0, 0.0, math.radians(30.0)))
        inner.parent = mount
        box.parent = inner
        world = (Matrix.LocRotScale(Vector(MOUNT_PLACE), Euler(UPRIGHT), Vector((1.0, 2.0, 1.0)))
                 @ Matrix.LocRotScale(Vector((0.3, 0.0, 0.0)), Euler((0.0, 0.0, math.radians(30.0))),
                                      Vector((1.0, 1.0, 1.0))))
    elif kind == "constraint":
        constraint = box.constraints.new("CHILD_OF")
        constraint.target = mount
        if hasattr(constraint, "set_inverse_pending"):
            constraint.set_inverse_pending = False
        constraint.inverse_matrix = Matrix.Identity(4)
    else:
        box.parent = mount
    if kind == "keyed":
        box.keyframe_insert(data_path="location", frame=1)
        box.keyframe_insert(data_path="rotation_euler", frame=1)
    if kind == "hidden":
        box.hide_viewport = True
    if kind == "shape_keys":
        box.shape_key_add(name="Basis")
        box.shape_key_add(name="Wide", from_mix=False).value = 0.0
    save(path)
    return box_world_size(world)


# Writes every fixture into a folder, each kind in a folder of its own.
def main(folder):
    for kind in GLASS_KINDS:
        make_glass_pane(os.path.join(folder, "glass_" + kind, "model.blend"), kind)
    for kind in LEAF_KINDS:
        make_leaf_card(os.path.join(folder, "leaf_" + kind, "model.blend"), kind)
    for kind in MODEL_KINDS:
        make_rotated_model(os.path.join(folder, "model_" + kind, "model.blend"), kind)


if __name__ == "__main__":
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not arguments:
        print("Give the folder to write the fixtures into after --.")
        sys.exit(1)
    main(os.path.abspath(arguments[0]))
