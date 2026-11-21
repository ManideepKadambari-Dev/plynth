import bpy

from . import Step
from .. import scene
from ..registry import CLEANUP_STEPS

ORDER = 60


# Renames objects and meshes after the asset id and puts them all in one collection of that name.
def run(ctx):
    asset_id = ctx.params["asset_id"]
    objects = sorted(scene.mesh_objects(scene.scene_objects()), key=lambda obj: obj.name)

    for index, obj in enumerate(objects):
        obj.name = "__plynth_tmp_%d" % index
        obj.data.name = "__plynth_tmp_%d" % index
    for index, obj in enumerate(objects):
        name = asset_id if len(objects) == 1 else "%s_%02d" % (asset_id, index + 1)
        obj.name = name
        obj.data.name = name

    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    collection = bpy.data.collections.new(asset_id)
    bpy.context.scene.collection.children.link(collection)
    for obj in objects:
        for owner in list(obj.users_collection):
            owner.objects.unlink(obj)
        collection.objects.link(obj)


CLEANUP_STEPS.register("name_objects", Step(ORDER, "Naming objects", run))
