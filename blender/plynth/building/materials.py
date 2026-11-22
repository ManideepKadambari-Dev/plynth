import bpy

from . import Step
from .. import surfaces
from ..registry import BUILD_STEPS

ORDER = 80


# Gives every surface material the build made its neutral look.
def run(ctx):
    for name, look in surfaces.LOOKS.items():
        mat = bpy.data.materials.get(name)
        if mat is not None:
            surfaces.apply_look(mat, look)


BUILD_STEPS.register("materials", Step(ORDER, "Applying materials", run))
