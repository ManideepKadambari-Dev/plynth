import os

import bpy

from . import Step
from ..jobs import WorkerError
from ..registry import EXPORT_STEPS

ORDER = 10


# Returns true for an object that belongs to the building: a built mesh or a furniture item.
def is_plynth_object(obj):
    kind = obj.get("plynth_kind")
    return kind is not None and kind != "collision" and (obj.type == "MESH" or obj.instance_type == "COLLECTION")


# Opens building.blend. It is never saved. A file that is missing or will not open is import_failed, and a
# file with nothing the build made in it is no_geometry.
def run(ctx):
    path = ctx.params["blend_file"]
    if not os.path.isfile(path):
        raise WorkerError("import_failed", "building.blend does not exist. Build the project first.")
    try:
        bpy.ops.wm.open_mainfile(filepath=path, load_ui=False)
    except Exception as error:
        raise WorkerError("import_failed", "building.blend could not be opened: %s" % error)
    if not any(is_plynth_object(o) for o in bpy.context.scene.collection.all_objects):
        raise WorkerError("no_geometry", "building.blend has no walls, floors or furniture to export.")


EXPORT_STEPS.register("open_blend", Step(ORDER, "Opening building.blend", run))
