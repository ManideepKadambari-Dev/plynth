import bpy
from mathutils import Matrix, Vector

from . import Step
from .. import scene
from ..registry import CLEANUP_STEPS

ORDER = 50


# Moves the model so the bottom centre of its bounding box is at the world origin.
def run(ctx):
    bpy.context.view_layer.update()
    objects = scene.scene_objects()
    low, high = scene.world_bounds(objects)
    shift = Vector((-(low.x + high.x) / 2.0, -(low.y + high.y) / 2.0, -low.z))
    move = Matrix.Translation(shift)
    for obj in scene.mesh_objects(objects):
        obj.data.transform(move)
        obj.data.update()


CLEANUP_STEPS.register("set_pivot", Step(ORDER, "Setting the pivot", run))
