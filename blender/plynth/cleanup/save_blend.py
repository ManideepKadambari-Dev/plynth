import os

import bpy

from . import Step
from ..jobs import WorkerError
from ..registry import CLEANUP_STEPS

ORDER = 100


# Points every image file inside the output folder at it by a path relative to asset.blend. The loaded pixels
# are kept, so nothing is read again from a path the unsaved scene cannot resolve.
def relative_image_paths(output):
    root = os.path.normcase(os.path.abspath(output))
    for image in bpy.data.images:
        if image.source != "FILE" or not image.filepath or image.packed_file is not None:
            continue
        path = os.path.abspath(bpy.path.abspath(image.filepath, start=output))
        if os.path.normcase(path).startswith(root + os.sep):
            image.filepath_raw = "//" + os.path.relpath(path, output).replace("\\", "/")


# Saves asset.blend into the output folder, with image paths relative to that folder.
def run(ctx):
    output = ctx.params["output_folder"]
    target = os.path.join(output, "asset.blend")
    try:
        os.makedirs(output, exist_ok=True)
        relative_image_paths(output)
        bpy.context.scene.unit_settings.system = "METRIC"
        bpy.context.scene.unit_settings.scale_length = 1.0
        bpy.ops.wm.save_as_mainfile(filepath=target, compress=False, relative_remap=False, copy=False)
    except Exception as error:
        raise WorkerError("save_failed", "asset.blend could not be saved: %s" % error)
    if not os.path.isfile(target):
        raise WorkerError("save_failed", "asset.blend was not written.")


CLEANUP_STEPS.register("save_blend", Step(ORDER, "Saving asset.blend", run))
