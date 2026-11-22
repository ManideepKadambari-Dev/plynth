import os

from . import Step
from .. import exporters
from ..exporters import fbx  # noqa: F401
from ..registry import CLEANUP_STEPS

ORDER = 105


# Writes asset.fbx beside asset.blend for the live preview. A failure is a warning and the import goes on.
def run(ctx):
    target = os.path.join(ctx.params["output_folder"], "asset.fbx")
    try:
        exporters.write_file("fbx", None, target, "RELATIVE")
        ctx.state["fbx_file"] = "asset.fbx"
    except Exception as error:
        ctx.state["fbx_file"] = None
        ctx.warn("fbx_failed", "asset.fbx could not be written: %s" % error)


CLEANUP_STEPS.register("export_fbx", Step(ORDER, "Exporting asset.fbx", run))
