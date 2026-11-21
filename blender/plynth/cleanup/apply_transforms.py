import bpy

from . import Step
from .. import scene
from ..registry import CLEANUP_STEPS

ORDER = 40


# Moves each object's position, turn and scale into its mesh so every transform is the identity.
def run(ctx):
    bpy.context.view_layer.update()
    scene.apply_object_transforms(scene.scene_objects())


CLEANUP_STEPS.register("apply_transforms", Step(ORDER, "Applying transforms", run))
