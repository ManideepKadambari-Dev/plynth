import os

from . import Step
from .. import exporters, scene
from ..exporters import fbx  # noqa: F401
from ..jobs import WorkerError
from ..registry import EXPORT_STEPS

ORDER = 65


# Writes every kit piece and its UCX_ meshes into the kit file of the output folder, when the job has a kit and
# the group files were written. Fills ctx.state["kit_output"] and ["kit_path"]; both stay empty otherwise.
def run(ctx):
    kit = ctx.params.get("kit")
    ctx.state["kit_output"] = None
    ctx.state["kit_path"] = None
    if not kit or not ctx.state.get("written"):
        return
    objects = ctx.state["kit_objects"]
    target = os.path.join(ctx.params["output_folder"], kit["file"])
    try:
        os.makedirs(ctx.params["output_folder"], exist_ok=True)
        exporters.write_file("fbx", objects, target, "RELATIVE")
    except Exception as error:
        raise WorkerError("export_failed", "%s could not be written: %s" % (kit["file"], error))
    pieces = [o for o in objects if o.name in ctx.state["kit_pieces"]]
    ctx.state["kit_path"] = target
    ctx.state["kit_output"] = {"file": kit["file"], "pieces": len(pieces), "objects": len(objects),
                               "triangles": scene.triangle_count(pieces)}


EXPORT_STEPS.register("write_kit", Step(ORDER, "Writing the kit", run))
