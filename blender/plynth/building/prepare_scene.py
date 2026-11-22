import bpy

from . import Step
from .. import scene
from ..registry import BUILD_STEPS

ORDER = 5


# Empties the scene, sets metric units and makes one collection per floor, named after the floor id.
def run(ctx):
    scene.clear_scene()
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 1.0
    ctx.state["collections"] = {}
    for floor in ctx.params["floors"]:
        collection = bpy.data.collections.new(floor["id"])
        bpy.context.scene.collection.children.link(collection)
        ctx.state["collections"][floor["id"]] = collection


BUILD_STEPS.register("prepare_scene", Step(ORDER, "Preparing the scene", run))
