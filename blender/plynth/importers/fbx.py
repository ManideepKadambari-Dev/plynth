import bpy

from . import ImporterEntry
from ..registry import IMPORTERS


# Imports an FBX file. Blender applies the file's own unit scale.
def load(ctx, path):
    bpy.ops.import_scene.fbx(filepath=path)


IMPORTERS.register("fbx", ImporterEntry((".fbx",), load))
