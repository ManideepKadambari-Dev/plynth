import os

import bpy

from . import Step
from ..jobs import WorkerError
from ..registry import BUILD_STEPS

ORDER = 120


# Saves building.blend under a temporary name and renames it when done, so a failed build keeps the last good file.
def run(ctx):
    folder = ctx.params["output_folder"]
    target = os.path.join(folder, ctx.params["blend_file"])
    temporary = os.path.join(folder, "building.part.blend")
    try:
        os.makedirs(folder, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=temporary, compress=False, relative_remap=False, copy=True)
        if not os.path.isfile(temporary):
            raise RuntimeError("no file was written")
        os.replace(temporary, target)
    except Exception as error:
        if os.path.exists(temporary):
            os.remove(temporary)
        raise WorkerError("save_failed", "building.blend could not be saved: %s" % error)


BUILD_STEPS.register("save_blend", Step(ORDER, "Saving building.blend", run))
