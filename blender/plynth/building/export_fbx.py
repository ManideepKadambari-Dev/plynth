import os

from . import Step
from .. import exporters, scene
from ..exporters import fbx  # noqa: F401
from ..jobs import WorkerError
from ..registry import BUILD_STEPS

ORDER = 130


# Exports the whole scene to building.fbx under a temporary name and renames it. Skipped when the job has no
# fbx_file. Counts the objects and triangles that were exported.
def run(ctx):
    objects = scene.mesh_objects(scene.scene_objects())
    ctx.state["objects"] = len(objects)
    ctx.state["triangles"] = scene.triangle_count(objects)
    name = ctx.params.get("fbx_file")
    if not name:
        ctx.state["fbx_file"] = None
        return
    target = os.path.join(ctx.params["output_folder"], name)
    try:
        exporters.write_file("fbx", None, target, "ABSOLUTE")
    except Exception as error:
        raise WorkerError("export_failed", "building.fbx could not be written: %s" % error)
    ctx.state["fbx_file"] = name


BUILD_STEPS.register("export_fbx", Step(ORDER, "Exporting the FBX", run))
