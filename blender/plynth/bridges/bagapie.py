import bpy

from . import Scope, enable_addon, realize, register, select_only
from ..generating import AddonUnavailable

MODULES = ("bagapie",)

BASE_WALL = "Input_35"
CAP_FLASHING = "Input_44"
FENCE_HEIGHT = "Input_5"
POST_HEIGHT = "Input_18"
POST_DISTANCE = "Input_17"
USE_HORIZONTAL = "Input_43"
FENCE_OFFSET = "Input_4"
SIDE_OFFSET = "Input_55"
END_OFFSET = "Input_36"
RANDOM_INPUTS = ("Input_7", "Input_50", "Input_34", "Input_16")


# Says whether Bagapie is installed and can be switched on.
def available():
    return enable_addon(MODULES)


# Raises AddonUnavailable when Bagapie cannot be used.
def require():
    if not available():
        raise AddonUnavailable("The add-on Bagapie is not installed or cannot be enabled.")


# Sets one input of a geometry nodes modifier by its socket identifier, in the way this Blender version allows.
def set_input(modifier, identifier, value):
    try:
        modifier.properties.inputs[identifier]["value"] = value
    except (AttributeError, KeyError, TypeError):
        modifier[identifier] = value


# Makes a fence along a path of (x, y, z) points: posts every post_spacing metres and rails between them.
# Returns a list with one mesh object, with no modifiers and no curve left behind.
def fence(points, height=1.0, post_spacing=1.0, rails=True):
    require()
    if len(points) < 2:
        raise ValueError("A fence needs at least two points.")
    scope = Scope()
    try:
        curve = bpy.data.curves.new("Fence_path", "CURVE")
        curve.dimensions = "3D"
        spline = curve.splines.new("POLY")
        spline.points.add(len(points) - 1)
        for point, (x, y, z) in zip(spline.points, points):
            point.co = (x, y, z, 1.0)
        path = bpy.data.objects.new("Fence_path", curve)
        bpy.context.scene.collection.objects.link(path)
        select_only([path])
        result = bpy.ops.bagapie.fence()
        if "FINISHED" not in result:
            raise RuntimeError("Bagapie did not make the fence.")
        if path.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        modifier = path.modifiers[0]
        set_input(modifier, BASE_WALL, False)
        set_input(modifier, CAP_FLASHING, False)
        set_input(modifier, USE_HORIZONTAL, bool(rails))
        set_input(modifier, FENCE_HEIGHT, float(height))
        set_input(modifier, POST_HEIGHT, float(height) * 0.95)
        set_input(modifier, POST_DISTANCE, float(post_spacing))
        for identifier in (FENCE_OFFSET, SIDE_OFFSET, END_OFFSET):
            set_input(modifier, identifier, 0.0)
        for identifier in RANDOM_INPUTS:
            set_input(modifier, identifier, 0.0)
        path.update_tag()
        merged = realize([path], "Fence")
    except Exception:
        scope.clean()
        raise
    scope.clean(keep=[merged])
    return [merged]


class Api:
    available = staticmethod(available)
    fence = staticmethod(fence)


register("bagapie", MODULES, Api)
