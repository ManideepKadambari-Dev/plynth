import bpy

from . import ImporterEntry
from ..registry import IMPORTERS


# Copies every object of another .blend file into the current scene, with the data they use.
def load(ctx, path):
    with bpy.data.libraries.load(path, link=False) as (source, target):
        target.objects = list(source.objects)
    scene_collection = bpy.context.scene.collection
    for obj in target.objects:
        if obj is not None:
            scene_collection.objects.link(obj)


IMPORTERS.register("blend", ImporterEntry((".blend",), load))
