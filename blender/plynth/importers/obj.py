import bpy

from . import ImporterEntry
from ..registry import IMPORTERS


# Imports an OBJ file with Blender's default axes, so the file's front becomes -Y.
def load(ctx, path):
    bpy.ops.wm.obj_import(filepath=path)


IMPORTERS.register("obj", ImporterEntry((".obj",), load))
