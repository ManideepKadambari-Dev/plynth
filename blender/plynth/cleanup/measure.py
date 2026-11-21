import bpy

from . import Step
from .. import scene
from ..registry import CLEANUP_STEPS

ORDER = 80


# Measures the model: size, triangles, objects and materials.
def run(ctx):
    bpy.context.view_layer.update()
    objects = scene.mesh_objects(scene.scene_objects())
    ctx.state.update(scene.describe(objects))



CLEANUP_STEPS.register("measure", Step(ORDER, "Measuring", run))
