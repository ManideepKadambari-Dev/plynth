import bpy

from . import Scope, cursor_at_origin, close_holes, realize, register, enable_addon, stand_on_origin
from ..generating import AddonUnavailable

MODULES = ("archimesh",)


# Says whether Archimesh is installed and can be switched on.
def available():
    return enable_addon(MODULES)


# Raises AddonUnavailable when Archimesh cannot be used.
def require():
    if not available():
        raise AddonUnavailable("The add-on Archimesh is not installed or cannot be enabled.")


# Makes a column standing on z 0 and centred on the origin. shape is round or square, width is the diameter or
# the side, plates adds the square base and cap. Returns a list with one mesh object, with no modifiers.
def column(height=2.4, width=0.3, shape="round", plates=True, plate_width=None):
    require()
    plate = plate_width or width * 1.3
    scope = Scope()
    try:
        with cursor_at_origin():
            result = bpy.ops.mesh.archimesh_column(
                model="1" if shape == "round" else "2", keep_size=True,
                rad_top=width / 2, rad_mid=width / 2, rad_bottom=width / 2, col_height=height,
                col_sx=width, col_sy=width, cir_base=False, cir_top=False,
                box_base=plates, box_base_x=plate, box_base_y=plate, box_base_z=0.05,
                box_top=plates, box_top_x=plate, box_top_y=plate, box_top_z=0.05,
                crt_mat=False, crt_array=False)
        if "FINISHED" not in result:
            raise RuntimeError("Archimesh did not make the column.")
        made = scope.new_objects()
        merged = realize(made, "Column")
        close_holes(merged)
        stand_on_origin(merged)
    except Exception:
        scope.clean()
        raise
    scope.clean(keep=[merged])
    return [merged]


# Makes a straight stair going toward +Y, width along x, standing on z 0 and centred on the origin. Returns a
# list with one mesh object, with no modifiers.
def stairs(steps=10, width=1.0, depth=0.25, height=2.8):
    require()
    scope = Scope()
    try:
        with cursor_at_origin():
            result = bpy.ops.mesh.archimesh_stairs(
                model="1", curve=False, step_num=steps, max_width=width, depth=depth, shift=0.0,
                thickness=0.0, sizev=True, height=height / steps, crt_mat=False)
        if "FINISHED" not in result:
            raise RuntimeError("Archimesh did not make the stairs.")
        merged = realize(scope.new_objects(), "Stairs")
        stand_on_origin(merged)
    except Exception:
        scope.clean()
        raise
    scope.clean(keep=[merged])
    return [merged]


class Api:
    available = staticmethod(available)
    column = staticmethod(column)
    stairs = staticmethod(stairs)


register("archimesh", MODULES, Api)
