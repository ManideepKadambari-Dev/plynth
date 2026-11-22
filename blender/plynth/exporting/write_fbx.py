import os
import shutil

from . import Step
from .. import exporters, scene
from ..exporters import fbx  # noqa: F401
from ..jobs import WorkerError
from ..registry import EXPORT_STEPS

ORDER = 60


# Returns the objects of one group: the building meshes whose kind and plan id the group takes (an empty list
# takes everything), and the collision meshes that belong to them. A collision mesh with no object takes the
# group only when the group takes everything.
def group_objects(group, meshes, collision):
    kinds = set(group["kinds"])
    ids = set(group["ids"])
    chosen = [o for o in meshes
              if (not kinds or o.get("plynth_kind") in kinds) and (not ids or o.get("plynth_id") in ids)]
    names = {o.name for o in chosen}
    everything = not kinds and not ids
    chosen += [o for o in collision if o.get("plynth_collision_for") in names or
               (everything and o.get("plynth_collision_for") not in {m.name for m in meshes})]
    return sorted(chosen, key=lambda o: o.name)


# Writes every group into its file in the output folder when the job writes and no scene check failed (or the
# job is forced). Otherwise writes nothing and takes back the textures it copied.
def run(ctx):
    params = ctx.params
    failed = [c for c in ctx.state.get("check_results", []) if c["status"] == "fail"]
    ctx.state["written"] = False
    ctx.state["written_files"] = []
    if not params["write"] or (failed and not params["force"]):
        folder = os.path.join(params["output_folder"], params["textures_folder"])
        if params["write"] and os.path.isdir(folder):
            shutil.rmtree(folder, ignore_errors=True)
        ctx.state["textures"] = []
        return
    for group in params["groups"]:
        objects = group_objects(group, ctx.state["meshes"], ctx.state["collision"])
        if not objects:
            raise WorkerError("export_failed", "The group %s has no objects to write." % group["id"])
        target = os.path.join(params["output_folder"], group["file"])
        try:
            os.makedirs(params["output_folder"], exist_ok=True)
            exporters.write_file("fbx", objects, target, "RELATIVE")
        except Exception as error:
            raise WorkerError("export_failed", "%s could not be written: %s" % (group["file"], error))
        visible = [o for o in objects if o.get("plynth_kind") != "collision"]
        ctx.state["written_files"].append({"group": group["id"], "file": group["file"], "path": target,
                                           "objects": [o.name for o in objects],
                                           "triangles": scene.triangle_count(visible)})
    ctx.state["written"] = True


EXPORT_STEPS.register("write_fbx", Step(ORDER, "Writing the FBX", run))
