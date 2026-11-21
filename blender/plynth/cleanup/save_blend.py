import os

import bpy

from . import Step
from ..jobs import WorkerError
from ..registry import CLEANUP_STEPS

ORDER = 100


# Saves asset.blend into the output folder. Image paths stay as they are, relative to that folder.
def run(ctx):
    output = ctx.params["output_folder"]
    target = os.path.join(output, "asset.blend")
    try:
        os.makedirs(output, exist_ok=True)
        bpy.context.scene.unit_settings.system = "METRIC"
        bpy.context.scene.unit_settings.scale_length = 1.0
        bpy.ops.wm.save_as_mainfile(filepath=target, compress=False, relative_remap=False, copy=False)
    except Exception as error:
        raise WorkerError("save_failed", "asset.blend could not be saved: %s" % error)
    if not os.path.isfile(target):
        raise WorkerError("save_failed", "asset.blend was not written.")


CLEANUP_STEPS.register("save_blend", Step(ORDER, "Saving asset.blend", run))
