import bpy

from . import ImporterEntry
from ..registry import IMPORTERS


# Copies every object of another .blend file into the current scene, with the data they use. An object that
# its file leaves out of renders, by itself or by its collections, is marked hidden from render so the cleanup
# can remove it. The collections are only read for that and are not kept.
def load(ctx, path):
    with bpy.data.libraries.load(path, link=False) as (source, target):
        target.objects = list(source.objects)
        target.collections = list(source.collections)
    collections = [c for c in target.collections if c is not None]
    shown = shown_collections(collections)
    scene_collection = bpy.context.scene.collection
    for obj in target.objects:
        if obj is None:
            continue
        inside = [c for c in obj.users_collection if c in collections]
        if inside and not any(c in shown for c in inside):
            obj.hide_render = True
        scene_collection.objects.link(obj)
    for collection in collections:
        bpy.data.collections.remove(collection)


# Returns the collections that render: not hidden from render, and either at the top or inside one that renders.
def shown_collections(collections):
    parents = {c: [p for p in collections if c in p.children.values()] for c in collections}
    shown = set()
    changed = True
    while changed:
        changed = False
        for collection in collections:
            if collection in shown or collection.hide_render:
                continue
            if not parents[collection] or any(p in shown for p in parents[collection]):
                shown.add(collection)
                changed = True
    return shown


IMPORTERS.register("blend", ImporterEntry((".blend",), load))
