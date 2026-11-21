import bpy

from . import Step
from .. import scene
from ..registry import CLEANUP_STEPS

ORDER = 80


# Returns the names of the materials the given mesh objects use, sorted.
def material_names(objects):
    names = set()
    for obj in objects:
        for slot in obj.material_slots:
            if slot.material is not None:
                names.add(slot.material.name)
    return sorted(names)


# Measures the model: size, triangles, objects and materials.
def run(ctx):
    bpy.context.view_layer.update()
    objects = scene.mesh_objects(scene.scene_objects())
    width, depth, height = scene.world_size(objects)
    ctx.state["size"] = {"width": round(width, 6), "depth": round(depth, 6), "height": round(height, 6)}
    ctx.state["triangles"] = scene.triangle_count(objects)
    ctx.state["objects"] = len(objects)
    ctx.state["materials"] = material_names(objects)


CLEANUP_STEPS.register("measure", Step(ORDER, "Measuring", run))
