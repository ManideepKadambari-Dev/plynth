import math
import os
import time

import bpy
from mathutils import Vector

from . import scene as scene_tools
from .jobs import WorkerError

FRONT_AZIMUTH = 45.0
ELEVATION = 25.0
LENS_MM = 50.0
SENSOR_MM = 36.0
MARGIN = 0.08
SAMPLES = 64
OBJECT_NAMES = ("plynth_thumb_camera", "plynth_thumb_key", "plynth_thumb_fill", "plynth_thumb_rim")

# Name, azimuth from -Y toward +X in degrees, elevation in degrees, watts per square metre of distance,
# and the size of the light as a share of the model radius.
LIGHTS = [
    ("plynth_thumb_key", 5.0, 50.0, 28.0, 2.0),
    ("plynth_thumb_fill", 100.0, 20.0, 10.0, 3.0),
    ("plynth_thumb_rim", 200.0, 35.0, 22.0, 1.5),
]


# Converts one sRGB value from 0 to 1 to the linear value Blender colors use.
def to_linear(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


# Returns the point at a direction and distance from the centre, with azimuth from -Y toward +X.
def point_around(centre, azimuth, elevation, distance):
    a = math.radians(azimuth)
    e = math.radians(elevation)
    direction = Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e)))
    return centre + direction * distance


# Turns an object so it looks at a point.
def look_at(obj, target):
    direction = target - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


# Returns the centre and radius of the sphere around all mesh objects.
def bounding_sphere(objects):
    bounds = scene_tools.world_bounds(objects)
    if bounds is None:
        raise WorkerError("render_failed", "There is nothing to render.")
    low, high = bounds
    centre = (low + high) / 2.0
    radius = max((high - low).length / 2.0, 1e-6)
    return centre, radius


# Removes camera and lights left by an earlier render of this scene.
def remove_old_rig():
    for name in OBJECT_NAMES:
        obj = bpy.data.objects.get(name)
        if obj is not None:
            bpy.data.objects.remove(obj, do_unlink=True)


# Adds the camera and the three lights, sized to the model, and returns nothing.
def build_rig(centre, radius):
    remove_old_rig()
    scene = bpy.context.scene
    camera_data = bpy.data.cameras.new("plynth_thumb_camera")
    camera_data.lens = LENS_MM
    camera_data.sensor_width = SENSOR_MM
    camera_data.sensor_height = SENSOR_MM
    camera_data.sensor_fit = "AUTO"
    half_angle = math.atan((SENSOR_MM / 2.0) / LENS_MM)
    distance = radius * (1.0 + MARGIN) / math.sin(half_angle)
    camera_data.clip_start = max(distance * 0.01, 0.001)
    camera_data.clip_end = distance * 4.0 + radius * 4.0
    camera = bpy.data.objects.new("plynth_thumb_camera", camera_data)
    camera.location = point_around(centre, FRONT_AZIMUTH, ELEVATION, distance)
    scene.collection.objects.link(camera)
    bpy.context.view_layer.update()
    look_at(camera, centre)
    scene.camera = camera

    light_distance = max(radius * 4.0, distance)
    for name, azimuth, elevation, power, size in LIGHTS:
        data = bpy.data.lights.new(name, "AREA")
        data.shape = "SQUARE"
        data.size = radius * size
        data.color = (1.0, 1.0, 1.0)
        data.energy = power * light_distance * light_distance
        light = bpy.data.objects.new(name, data)
        light.location = point_around(centre, azimuth, elevation, light_distance)
        scene.collection.objects.link(light)
        bpy.context.view_layer.update()
        look_at(light, centre)


# Gives the scene a plain world of the given sRGB color.
def build_world(background):
    scene = bpy.context.scene
    world = bpy.data.worlds.get("plynth_thumb_world") or bpy.data.worlds.new("plynth_thumb_world")
    try:
        world.use_nodes = True
    except Exception:
        pass
    tree = world.node_tree
    node = tree.nodes.get("Background")
    if node is None:
        node = tree.nodes.new("ShaderNodeBackground")
        output = tree.nodes.get("World Output") or tree.nodes.new("ShaderNodeOutputWorld")
        tree.links.new(node.outputs["Background"], output.inputs["Surface"])
    node.inputs["Color"].default_value = (to_linear(background[0]), to_linear(background[1]),
                                          to_linear(background[2]), 1.0)
    node.inputs["Strength"].default_value = 1.0
    scene.world = world


# Sets a property when this Blender version has it.
def try_set(target, name, value):
    try:
        setattr(target, name, value)
    except (AttributeError, TypeError, ValueError):
        pass


ENGINE_IDS = {"cycles": ["CYCLES"], "eevee": ["BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"]}


# Selects the render engine called "eevee" or "cycles". The Eevee id changed between Blender versions, so
# each known id is tried. Returns false when none is accepted.
def select_engine(name):
    for engine in ENGINE_IDS.get(name, []):
        try:
            bpy.context.scene.render.engine = engine
            return True
        except (TypeError, ValueError):
            continue
    return False


# Sets the shared render settings: square size, Standard view transform, plain 8 bit RGB PNG.
def configure_output(size, target):
    scene = bpy.context.scene
    render = scene.render
    render.resolution_x = size
    render.resolution_y = size
    render.resolution_percentage = 100
    render.film_transparent = False
    render.dither_intensity = 0.0
    render.filepath = target
    render.use_file_extension = False
    render.image_settings.file_format = "PNG"
    render.image_settings.color_mode = "RGB"
    render.image_settings.color_depth = "8"
    try_set(render.image_settings, "color_management", "FOLLOW_SCENE")
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    try_set(scene.view_settings, "use_curve_mapping", False)
    scene.display_settings.display_device = "sRGB"


# Renders the current scene with one engine to the file in render.filepath. Raises on failure.
def render_with(name):
    scene = bpy.context.scene
    if not select_engine(name):
        raise WorkerError("render_failed", "The render engine '%s' is not available in this Blender." % name)
    if name == "cycles":
        try_set(scene.cycles, "device", "CPU")
        try_set(scene.cycles, "samples", SAMPLES)
        try_set(scene.cycles, "use_adaptive_sampling", False)
        try_set(scene.cycles, "use_denoising", False)
    else:
        try_set(scene.eevee, "taa_render_samples", SAMPLES)
    try:
        bpy.ops.render.render(write_still=True)
    except Exception as error:
        raise WorkerError("render_failed", "Rendering with %s failed: %s" % (name, error))


# Renders a three quarter view of every mesh object to output_file with the look in spec (render, size, engine,
# background). Tries Eevee first when asked and falls back to Cycles with an eevee_failed warning. Returns the
# engine used. Raises WorkerError render_failed when no engine worked.
def render_thumbnail(output_file, spec, warn):
    objects = scene_tools.mesh_objects(scene_tools.scene_objects())
    bpy.context.view_layer.update()
    centre, radius = bounding_sphere(objects)
    build_rig(centre, radius)
    build_world(spec["background"])
    temporary = output_file + ".part"
    configure_output(int(spec["size"]), temporary)
    wanted = spec["engine"]
    if wanted not in ("eevee", "cycles"):
        raise WorkerError("render_failed", "The thumbnail engine '%s' is not known." % wanted)

    order = ["eevee", "cycles"] if wanted == "eevee" else ["cycles"]
    last_error = None
    for name in order:
        if os.path.exists(temporary):
            os.remove(temporary)
        try:
            started = time.monotonic()
            render_with(name)
            if not os.path.isfile(temporary):
                raise WorkerError("render_failed", "The %s render wrote no picture." % name)
            os.replace(temporary, output_file)
            print("Thumbnail rendered with %s in %.2f s" % (name, time.monotonic() - started))
            return name
        except WorkerError as error:
            last_error = error
            if name == "eevee":
                warn("eevee_failed", "Eevee could not render the thumbnail, so Cycles was used: %s" % error.message)
    raise last_error
