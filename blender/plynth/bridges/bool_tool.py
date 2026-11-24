import contextlib

import bpy

from . import enable_addon, find_module, register, select_only
from ..generating import AddonUnavailable

MODULES = ("bool_tool",)


# Says whether Bool Tool is installed and can be switched on.
def available():
    return enable_addon(MODULES)


# Raises AddonUnavailable when Bool Tool cannot be used.
def require():
    if not available():
        raise AddonUnavailable("The add-on Bool Tool is not installed or cannot be enabled.")


# Uses the exact boolean solver while a call runs. The tool's default solver is the fast float one, which can
# give the wrong shape (a cut that keeps only the overlap). The preference is changed in this session only and put
# back afterwards.
@contextlib.contextmanager
def exact_solver():
    preferences = bpy.context.preferences.addons[find_module(MODULES)].preferences
    saved = preferences.solver
    preferences.solver = "EXACT"
    try:
        yield
    finally:
        preferences.solver = saved


# Cuts the cutter objects out of the target mesh object, in place, and removes the cutters and any modifier
# the tool left behind. Returns a list with the target.
def difference(target, cutters):
    require()
    cutters = list(cutters)
    if not cutters:
        return [target]
    select_only([target] + cutters)
    bpy.context.view_layer.objects.active = target
    with exact_solver():
        result = bpy.ops.object.boolean_auto_difference()
    if "FINISHED" not in result:
        raise RuntimeError("Bool Tool did not cut the shape.")
    select_only([target])
    for modifier in list(target.modifiers):
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    for cutter in cutters:
        try:
            mesh = cutter.data
            bpy.data.objects.remove(cutter, do_unlink=True)
        except ReferenceError:
            continue
        if not mesh.users:
            bpy.data.meshes.remove(mesh)
    return [target]


class Api:
    available = staticmethod(available)
    difference = staticmethod(difference)


register("bool_tool", MODULES, Api)
