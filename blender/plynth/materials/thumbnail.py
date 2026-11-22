import math
import os
import time

import bmesh
import bpy
from mathutils import Vector

from .. import scene as scene_tools
from .. import shading
from .. import thumbnail
from ..jobs import WorkerError

SPHERE_DIAMETER = 1.0
SPHERE_CENTRE = Vector((-0.57, 0.0, 0.5))
TILE_CENTRE = Vector((0.57, 0.0, 0.5))
TILE_TURN = 30.0
VIEW_CENTRE = Vector((0.0, 0.0, 0.5))
VIEW_WIDTH = 2.3
VIEW_DISTANCE = 8.0
LIGHT_RADIUS = 1.15
LIGHT_SCALE = 0.55


# Adds a mesh object holding the bmesh to the scene and returns it.
def add_object(name, bm, location):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    bpy.context.scene.collection.objects.link(obj)
    return obj


# Makes the sphere with UVs in metres divided by the tile size: u runs round the equator, v from pole to pole.
def make_sphere(material, tile_size):
    bm = bmesh.new()
    bm.loops.layers.uv.new("UVMap")
    bmesh.ops.create_uvsphere(bm, u_segments=96, v_segments=48, radius=SPHERE_DIAMETER / 2.0, calc_uvs=True)
    layer = bm.loops.layers.uv.active
    around = math.pi * SPHERE_DIAMETER / tile_size["width"]
    pole_to_pole = math.pi * SPHERE_DIAMETER / 2.0 / tile_size["height"]
    for face in bm.faces:
        face.smooth = True
        for loop in face.loops:
            loop[layer].uv = (loop[layer].uv[0] * around, loop[layer].uv[1] * pole_to_pole)
    obj = add_object("plynth_material_sphere", bm, SPHERE_CENTRE)
    obj.data.materials.append(material)
    return obj


# Makes the 1 m square tile, standing upright and turned 30 degrees toward the lights, with UVs in metres
# divided by the tile size.
def make_tile(material, tile_size):
    bm = bmesh.new()
    layer = bm.loops.layers.uv.new("UVMap")
    corners = [(-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)]
    face = bm.faces.new([bm.verts.new((x, y, 0.0)) for x, y in corners])
    for loop, (x, y) in zip(face.loops, corners):
        loop[layer].uv = ((x + 0.5) / tile_size["width"], (y + 0.5) / tile_size["height"])
    obj = add_object("plynth_material_tile", bm, TILE_CENTRE)
    obj.rotation_euler = (math.radians(90.0), 0.0, math.radians(TILE_TURN))
    obj.data.materials.append(material)
    return obj


# Adds the orthographic camera that looks straight at the pair, and the asset thumbnail's three lights, a
# little dimmer because a flat tile faces the key light more directly than a model does.
def build_rig():
    thumbnail.remove_old_rig()
    scene = bpy.context.scene
    data = bpy.data.cameras.new("plynth_thumb_camera")
    data.type = "ORTHO"
    data.ortho_scale = VIEW_WIDTH
    data.clip_start = 0.1
    data.clip_end = VIEW_DISTANCE * 3
    camera = bpy.data.objects.new("plynth_thumb_camera", data)
    camera.location = VIEW_CENTRE + Vector((0.0, -VIEW_DISTANCE, 0.0))
    camera.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.collection.objects.link(camera)
    scene.camera = camera
    light_distance = LIGHT_RADIUS * 4.0
    for name, azimuth, elevation, power, size in thumbnail.LIGHTS:
        light_data = bpy.data.lights.new(name, "AREA")
        light_data.shape = "SQUARE"
        light_data.size = LIGHT_RADIUS * size
        light_data.energy = LIGHT_SCALE * power * light_distance * light_distance
        light = bpy.data.objects.new(name, light_data)
        light.location = thumbnail.point_around(VIEW_CENTRE, azimuth, elevation, light_distance)
        scene.collection.objects.link(light)
        bpy.context.view_layer.update()
        thumbnail.look_at(light, VIEW_CENTRE)


# Renders the material as a sphere and a tilted tile side by side to output_file with Eevee only. paths maps
# each channel to its image file (a channel without a map is left out). Returns "eevee". Raises WorkerError
# render_failed, and shading.MapError when a map will not load.
def render(output_file, spec, paths, tile_size):
    scene_tools.clear_scene()
    material = bpy.data.materials.new("plynth_material_preview")
    try:
        shading.apply_maps(material, {channel: path for channel, path in paths.items() if path})
        make_sphere(material, tile_size)
        make_tile(material, tile_size)
        bpy.context.view_layer.update()
        build_rig()
        thumbnail.build_world(spec["background"])
        temporary = output_file + ".part"
        thumbnail.configure_output(int(spec["size"]), temporary)
        if os.path.exists(temporary):
            os.remove(temporary)
        started = time.monotonic()
        thumbnail.render_with_eevee()
        if not os.path.isfile(temporary):
            raise WorkerError("render_failed", "The Eevee render wrote no picture.")
        os.replace(temporary, output_file)
        print("Material thumbnail rendered with Eevee in %.2f s" % (time.monotonic() - started))
        return "eevee"
    finally:
        scene_tools.clear_scene()
