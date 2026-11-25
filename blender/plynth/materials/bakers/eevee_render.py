import os
import time

import bpy

from .. import (BakerEntry, ImageMissing, is_magenta, map_entry, read_pixels, to_bytes, unloadable_images, write_grey,
                write_png)
from ... import thumbnail
from ...jobs import WorkerError
from ...registry import BAKERS

ORDER = 20
DEFAULT_SIZE = 1024
TILE_NAME = "plynth_bake_tile"
CAMERA_NAME = "plynth_bake_camera"


# Says whether this baker can make the channel: anything that comes from a node of a Blender material.
def can_bake(ctx, plan):
    return plan.kind == "node"


# Adds a one metre tile at the origin with UVs from 0 to 1, and an orthographic camera straight above it that
# sees exactly the tile. Returns both objects.
def build_tile(material):
    mesh = bpy.data.meshes.new(TILE_NAME)
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)], [], [(0, 1, 2, 3)])
    layer = mesh.uv_layers.new(name="UVMap")
    for loop, uv in zip(mesh.polygons[0].loop_indices, [(0, 0), (1, 0), (1, 1), (0, 1)]):
        layer.data[loop].uv = uv
    mesh.materials.append(material)
    tile = bpy.data.objects.new(TILE_NAME, mesh)
    scene = bpy.context.scene
    scene.collection.objects.link(tile)
    data = bpy.data.cameras.new(CAMERA_NAME)
    data.type = "ORTHO"
    data.ortho_scale = 1.0
    data.clip_start = 0.1
    data.clip_end = 10.0
    camera = bpy.data.objects.new(CAMERA_NAME, data)
    camera.location = (0.5, 0.5, 2.0)
    scene.collection.objects.link(camera)
    scene.camera = camera
    return tile, camera


# Makes a copy of the material that shows one node output as emitted light and nothing else. A vector (the
# normal) is shown as vector * 0.5 + 0.5.
def emission_copy(reference, vector):
    material = bpy.data.materials[reference["material"]].copy()
    tree = material.node_tree
    outputs = [n for n in tree.nodes if n.bl_idname == "ShaderNodeOutputMaterial"]
    output = next((n for n in outputs if n.is_active_output and n.target in ("ALL", "EEVEE")), outputs[0])
    for name in ("Surface", "Displacement", "Volume"):
        for link in list(output.inputs[name].links):
            tree.links.remove(link)
    emission = tree.nodes.new("ShaderNodeEmission")
    emission.inputs["Strength"].default_value = 1.0
    source = tree.nodes[reference["node"]].outputs[reference["socket"]]
    if vector:
        scale = tree.nodes.new("ShaderNodeVectorMath")
        scale.operation = "MULTIPLY_ADD"
        scale.inputs[1].default_value = (0.5, 0.5, 0.5)
        scale.inputs[2].default_value = (0.5, 0.5, 0.5)
        tree.links.new(source, scale.inputs[0])
        source = scale.outputs["Vector"]
    tree.links.new(source, emission.inputs["Color"])
    tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])
    return material


# Sets the render up to write the emitted values untouched: no filter, one sample, no dither, and the Raw view
# transform (Standard for base color, which is stored as sRGB). Returns the world made for it.
def configure(size, target, channel):
    scene = bpy.context.scene
    thumbnail.configure_output(size, target)
    scene.view_settings.view_transform = "Standard" if channel == "base_color" else "Raw"
    scene.render.filter_size = 0.0
    scene.render.use_compositing = False
    scene.render.use_sequencer = False
    world = bpy.data.worlds.new("plynth_bake_world")
    scene.world = world
    if not thumbnail.select_eevee():
        raise WorkerError("bake_failed", "Eevee is not available in this Blender.")
    thumbnail.try_set(scene.eevee, "taa_render_samples", 1)
    return world


# Hides every other object from the render and returns what to restore.
def hide_others():
    hidden = [(obj, obj.hide_render) for obj in bpy.context.scene.collection.all_objects]
    for obj, _ in hidden:
        obj.hide_render = True
    return hidden


# Removes the objects, their data and the world made for a render, and shows the hidden objects again.
def tear_down(objects, world, hidden):
    for obj, state in hidden:
        obj.hide_render = state
    for obj in objects:
        if obj is not None:
            data = obj.data
            bpy.data.objects.remove(obj)
            if data.users == 0:
                (bpy.data.meshes if isinstance(data, bpy.types.Mesh) else bpy.data.cameras).remove(data)
    if world is not None:
        bpy.data.worlds.remove(world)


# Raises ImageMissing when an image the node output reads has no pixels, since Eevee would show it as magenta.
def check_images(reference, channel):
    material = bpy.data.materials[reference["material"]]
    missing = unloadable_images(material.node_tree.nodes[reference["node"]])
    if missing:
        raise ImageMissing("The %s of the material %s uses the image %s, which could not be loaded, so its map "
                           "would be magenta." % (channel.replace("_", " "), material.name, ", ".join(missing)))


# Renders the channel's node output onto one tile with Eevee and writes the map. Returns its entry, or None
# when an opacity map is all opaque. Raises ImageMissing for a missing image and a base color that comes out
# magenta.
def bake(ctx, plan, textures):
    size = int(ctx.params.get("bake_size", DEFAULT_SIZE))
    channel = plan.channel
    check_images(plan.node, channel)
    material = emission_copy(plan.node, channel == "normal")
    target = os.path.join(textures, "_render_%s.png" % channel)
    tile = camera = world = None
    hidden = hide_others()
    started = time.monotonic()
    try:
        tile, camera = build_tile(material)
        world = configure(size, target, channel)
        bpy.context.view_layer.update()
        try:
            bpy.ops.render.render(write_still=True)
        except Exception as error:
            raise WorkerError("bake_failed", "Rendering the %s map failed: %s" % (channel, error))
        if not os.path.isfile(target):
            raise WorkerError("bake_failed", "The Eevee render of the %s map wrote no picture." % channel)
        pixels = read_pixels(target)
    finally:
        if os.path.exists(target):
            os.remove(target)
        tear_down((tile, camera), world, hidden)
        bpy.data.materials.remove(material)
    print("Baked %s with Eevee at %d px in %.2f s" % (channel, size, time.monotonic() - started))
    if channel == "base_color" and is_magenta(pixels):
        raise ImageMissing("The base color of the material %s came out magenta, the colour Blender shows for an "
                           "image it cannot load." % plan.node["material"])
    path = os.path.join(textures, channel + ".png")
    if channel in ("base_color", "normal"):
        write_png(path, to_bytes(pixels[:, :, :3]))
    else:
        plane = pixels[:, :, 0]
        if channel == "opacity" and plane.min() >= 0.999:
            return None
        write_grey(path, plane)
    return map_entry(channel, "baked", "Eevee, %d px" % size)


BAKERS.register("eevee_render", BakerEntry(ORDER, can_bake, bake))
