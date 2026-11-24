import bpy

from . import Step
from .. import scene, surfaces
from ..registry import BUILD_STEPS

ORDER = 5
GENERATED = "Generated"


# Empties the scene, sets metric units and makes one collection per floor, named after the floor id. Then it
# makes an empty material for every slot of the job, so a furniture material appended later with the same name
# gets a number added and keeps its own look, maps and name in the materials file.
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
    for name in slot_names(ctx.params):
        surfaces.material(name)


# Returns the names of the slots of the job, with Generated when a floor has generator items.
def slot_names(params):
    names = [entry["name"] for entry in params.get("slots", [])]
    if GENERATED not in names and any(floor.get("generator_items") for floor in params["floors"]):
        names.append(GENERATED)
    return names


BUILD_STEPS.register("prepare_scene", Step(ORDER, "Preparing the scene", run))
