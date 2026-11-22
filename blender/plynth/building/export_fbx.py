import json
import os

import bpy

from . import Step, preview_textures
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


# Returns the objects building.fbx holds: everything in the scene except the collision meshes.
def fbx_objects():
    return [o for o in scene.scene_objects() if o.get("plynth_kind") != "collision"]


# Makes instances real, points big images at preview copies, exports the scene without the collision meshes to
# building.fbx under a temporary name and renames it. The export is skipped when the job has no fbx_file.
# Counts the objects and triangles that went into it. building.blend was saved before this step, so it keeps
# the instances and the full size images.
def run(ctx):
    realize_instances()
    objects = fbx_objects()
    meshes = scene.mesh_objects(objects)
    ctx.state["objects"] = len(meshes)
    ctx.state["triangles"] = scene.triangle_count(meshes)
    name = ctx.params.get("fbx_file")
    if not name:
        ctx.state["fbx_file"] = None
        ctx.state["materials_file"] = None
        return
    try:
        copies = preview_textures.make_preview_copies(ctx, meshes)
    except Exception as error:
        raise WorkerError("export_failed", "The preview textures could not be made: %s" % error)
    ctx.state["preview_copies"] = copies
    target = os.path.join(ctx.params["output_folder"], name)
    try:
        exporters.write_file("fbx", objects, target, "ABSOLUTE")
    except Exception as error:
        raise WorkerError("export_failed", "building.fbx could not be written: %s" % error)
    ctx.state["fbx_file"] = name
    write_materials(ctx)


# Writes materials.json beside the FBX under a temporary name and renames it. It lists the images of every
# built material and the slot order of every built object, for the 3D preview.
def write_materials(ctx):
    name = ctx.params.get("materials_file") or "materials.json"
    target = os.path.join(ctx.params["output_folder"], name)
    temporary = os.path.join(ctx.params["output_folder"], "materials.part.json")
    copies = ctx.state.get("preview_copies", {})
    entries = ctx.state.get("built_materials", []) + ctx.state.get("asset_materials", [])
    listed = [dict(entry, maps=preview_textures.repoint_maps(entry["maps"], copies)) for entry in entries]
    data = {"format": "plynth-build-materials", "version": 1, "materials": listed,
            "objects": ctx.state.get("built_objects", [])}
    try:
        with open(temporary, "w", encoding="utf-8") as handle:
            json.dump(data, handle, indent=1)
        os.replace(temporary, target)
    except OSError as error:
        if os.path.exists(temporary):
            os.remove(temporary)
        raise WorkerError("export_failed", "materials.json could not be written: %s" % error)
    ctx.state["materials_file"] = name


BUILD_STEPS.register("export_fbx", Step(ORDER, "Exporting the FBX", run))
