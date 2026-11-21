import os

import bpy

from . import Step
from .. import exporters, scene
from ..exporters import fbx  # noqa: F401
from ..jobs import WorkerError
from ..registry import BUILD_STEPS

ORDER = 130


# Turns every collection instance into real objects that share the asset mesh data, children of the instance
# empty. Done after building.blend is saved, so that file keeps the cheap instances.
def realize_instances():
    for empty in [o for o in bpy.data.objects if o.type == "EMPTY" and o.instance_type == "COLLECTION"]:
        collection = empty.instance_collection
        if collection is None:
            continue
        parent_collections = list(empty.users_collection)
        for source in list(collection.all_objects):
            if source.type != "MESH":
                continue
            copy = bpy.data.objects.new("%s_%s" % (empty.name, source.name), source.data)
            copy.parent = empty
            copy.matrix_parent_inverse.identity()
            copy.matrix_basis = source.matrix_world.copy()
            for key in ("plynth_id", "plynth_kind"):
                if key in empty:
                    copy[key] = empty[key]
            for target in parent_collections:
                target.objects.link(copy)
        empty.instance_type = "NONE"
        empty.instance_collection = None
    bpy.context.view_layer.update()


# Makes instances real, exports the whole scene to building.fbx under a temporary name and renames it. The
# export is skipped when the job has no fbx_file. Counts the objects and triangles that went into it.
def run(ctx):
    realize_instances()
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
