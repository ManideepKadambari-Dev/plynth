import bpy

from . import ImporterEntry
from ..registry import IMPORTERS


# Imports a glTF or GLB file.
def load(ctx, path):
    bpy.ops.import_scene.gltf(filepath=path)


IMPORTERS.register("gltf", ImporterEntry((".gltf", ".glb"), load))
