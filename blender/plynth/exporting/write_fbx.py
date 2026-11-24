import os
import shutil

from . import Step
from .. import exporters, scene
from ..exporters import fbx  # noqa: F401
from ..jobs import WorkerError
from ..registry import EXPORT_STEPS
from .groups import assign

ORDER = 60


# Writes every group that has objects into its file in the output folder when the job writes and no scene
# check failed (or the job is forced). A group with no object is listed in empty_groups. Otherwise writes nothing and takes back the textures it copied.
def run(ctx):
    params = ctx.params
    failed = [c for c in ctx.state.get("check_results", []) if c["status"] == "fail"]
    ctx.state["written"] = False
    ctx.state["written_files"] = []
    ctx.state["empty_groups"] = []
    if not params["write"] or (failed and not params["force"]):
        folder = os.path.join(params["output_folder"], params["textures_folder"])
        if params["write"] and os.path.isdir(folder):
            shutil.rmtree(folder, ignore_errors=True)
        ctx.state["textures"] = []
        return
    found, _ = assign(params["groups"], ctx.state["meshes"] + ctx.state.get("lights", []), ctx.state["collision"])
    for group in params["groups"]:
        objects = found[group["id"]]
        if not objects:
            ctx.state["empty_groups"].append(group["id"])
            continue
        target = os.path.join(params["output_folder"], group["file"])
        try:
            os.makedirs(params["output_folder"], exist_ok=True)
            exporters.write_file("fbx", objects, target, "RELATIVE")
        except Exception as error:
            raise WorkerError("export_failed", "%s could not be written: %s" % (group["file"], error))
        visible = [o for o in objects if o.get("plynth_kind") != "collision" and o.type == "MESH"]
        ctx.state["written_files"].append({"group": group["id"], "file": group["file"], "path": target,
                                           "objects": [o.name for o in objects],
                                           "triangles": scene.triangle_count(visible)})
    ctx.state["written"] = bool(ctx.state["written_files"])
    if not ctx.state["written"]:
        shutil.rmtree(os.path.join(params["output_folder"], params["textures_folder"]), ignore_errors=True)
        ctx.state["textures"] = []


EXPORT_STEPS.register("write_fbx", Step(ORDER, "Writing the FBX", run))
