import math
import os

import unreal

from . import ue
from .steps.level import has_tag

WIDTH = 1280
HEIGHT = 720
FIELD_OF_VIEW = 50.0


# Saves a picture of the building in the open level to the path with a scene capture placed to see all of
# its actors, and returns the path, or None when no picture could be made. The capture actor is removed.
def take(ctx, path):
    world = ue.editor_world()
    tag = ctx.job["level"]["tag"]
    ours = [actor for actor in ue.actors().get_all_level_actors() if has_tag(actor, tag)]
    if not ours:
        ue.warn("no actor of the building is in the level, so there is no picture")
        return None
    center, radius = view_of(ours)
    folder, name = os.path.split(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    if os.path.isfile(path):
        os.remove(path)
    direction = normalized((-1.0, -1.3, 0.8))
    distance = radius / math.sin(math.radians(FIELD_OF_VIEW) / 2.0) * 1.05
    location = unreal.Vector(center[0] + direction[0] * distance, center[1] + direction[1] * distance,
                             center[2] + direction[2] * distance)
    rotation = unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(*center))
    capture = None
    try:
        target = unreal.RenderingLibrary.create_render_target2d(
            world, WIDTH, HEIGHT, unreal.TextureRenderTargetFormat.RTF_RGBA8)
        capture = ue.actors().spawn_actor_from_class(unreal.SceneCapture2D, location, rotation)
        component = capture.get_editor_property("capture_component2d")
        component.set_editor_property("texture_target", target)
        component.set_editor_property("fov_angle", FIELD_OF_VIEW)
        component.set_editor_property("capture_every_frame", False)
        component.set_editor_property("capture_on_movement", False)
        component.set_editor_property("capture_source", capture_source())
        component.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, target, folder, name)
    except Exception as error:
        ue.warn("the picture of the level could not be made: %s" % error)
    finally:
        if capture is not None:
            ue.actors().destroy_actor(capture)
    return path if os.path.isfile(path) else None


# Returns the middle of the actors' bounds and the radius of a ball around them, in centimetres.
def view_of(actors):
    low = [float("inf")] * 3
    high = [float("-inf")] * 3
    for actor in actors:
        origin, extent = actor.get_actor_bounds(False)
        for axis, (o, e) in enumerate(((origin.x, extent.x), (origin.y, extent.y), (origin.z, extent.z))):
            low[axis] = min(low[axis], o - e)
            high[axis] = max(high[axis], o + e)
    center = [(low[axis] + high[axis]) / 2.0 for axis in range(3)]
    radius = max(100.0, 0.5 * math.sqrt(sum((high[axis] - low[axis]) ** 2 for axis in range(3))))
    return center, radius


# Returns a vector scaled to length one.
def normalized(vector):
    length = math.sqrt(sum(value * value for value in vector))
    return tuple(value / length for value in vector)


# Returns the lit final image when the level has a light, else the plain surface colors, so a level without
# lights still gives a picture that shows the building.
def capture_source():
    has_light = any(isinstance(actor, unreal.Light) for actor in ue.actors().get_all_level_actors())
    name = "SCS_FINAL_COLOR_LDR" if has_light else "SCS_BASE_COLOR"
    return getattr(unreal.SceneCaptureSource, name)
