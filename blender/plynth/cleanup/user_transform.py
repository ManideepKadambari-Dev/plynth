import math

from mathutils import Matrix

from . import Step
from .. import scene
from ..registry import CLEANUP_STEPS

ORDER = 30


# Scales and turns every object around the world origin by the amounts the user chose.
def run(ctx):
    factor = float(ctx.params.get("scale", 1.0))
    angle = int(ctx.params.get("rotate_z", 0))
    if factor == 1.0 and angle == 0:
        return
    change = Matrix.Diagonal((factor, factor, factor, 1.0)) @ Matrix.Rotation(math.radians(angle), 4, "Z")
    for obj in scene.scene_objects():
        obj.matrix_world = change @ obj.matrix_world


CLEANUP_STEPS.register("user_transform", Step(ORDER, "Applying scale and rotation", run))
