import bpy
from mathutils import Matrix

from . import COLLISION_KIND, Step
from .. import scene
from ..jobs import WorkerError
from ..registry import EXPORT_STEPS

ORDER = 20
KEPT_PROPERTIES = ("plynth_id", "plynth_kind", "plynth_asset")


# Returns the world matrix of an object worked out from its parents, so objects of a collection that is not in
# the scene need no scene update.
def world_matrix(obj):
    if obj.parent is None:
        return obj.matrix_basis.copy()
    return world_matrix(obj.parent) @ obj.matrix_parent_inverse @ obj.matrix_basis


# Makes every layer collection and object of the scene visible and selectable, so the exporter can select them.
def show_everything():
    def walk(layer):
        layer.exclude = False
        layer.hide_viewport = False
        for child in layer.children:
            walk(child)

    walk(bpy.context.view_layer.layer_collection)
    for obj in scene.scene_objects():
        obj.hide_viewport = False
        obj.hide_select = False
        try:
            obj.hide_set(False)
        except RuntimeError:
            pass


# Replaces an item's instance with real objects and joins its parts into one mesh named after the item.
# Returns the new mesh, or None when the asset has no mesh.
def realize_item(empty):
    collection = empty.instance_collection
    target_collections = list(empty.users_collection) or [bpy.context.scene.collection]
    name = empty.name
    placement = empty.matrix_world @ Matrix.Translation(-collection.instance_offset)
    parts = []
    for source in sorted(collection.all_objects, key=lambda o: o.name):
        if source.type != "MESH":
            continue
        part = bpy.data.objects.new(source.name, source.data.copy())
        matrix = placement @ world_matrix(source)
        part.data.transform(matrix)
        if matrix.determinant() < 0:
            part.data.flip_normals()
        for target in target_collections:
            target.objects.link(part)
        parts.append(part)
    properties = {key: empty[key] for key in KEPT_PROPERTIES if key in empty}
    empty.name = name + "_instance"
    bpy.data.objects.remove(empty, do_unlink=True)
    if not parts:
        return None
    joined = parts[0]
    if len(parts) > 1:
        with bpy.context.temp_override(active_object=joined, object=joined, selected_objects=parts,
                                       selected_editable_objects=parts):
            bpy.ops.object.join()
    joined.name = name
    joined.data.name = name
    for key, value in properties.items():
        joined[key] = value
    return joined


# Makes the scene ready: instances become real and one mesh per item, curves become meshes, modifiers are
# applied, empties and parents go, every mesh has its own data and an identity transform. Everything then sits
# at the building origin (DESIGN.md section 7.16).
def run(ctx):
    show_everything()
    bpy.context.view_layer.update()
    for empty in [o for o in scene.scene_objects() if o.type == "EMPTY" and o.instance_type == "COLLECTION"
                  and o.instance_collection is not None]:
        realize_item(empty)
    scene.convert_to_meshes(scene.scene_objects())
    bpy.context.view_layer.update()
    scene.bake_modifiers(scene.scene_objects())
    scene.unparent(scene.scene_objects())
    for empty in [o for o in scene.scene_objects() if o.type == "EMPTY"]:
        bpy.data.objects.remove(empty, do_unlink=True)
    meshes = scene.mesh_objects(scene.scene_objects())
    scene.apply_object_transforms(meshes)
    bpy.context.view_layer.update()
    ctx.state["meshes"] = [o for o in meshes if o.get("plynth_kind") != COLLISION_KIND]
    ctx.state["collision"] = [o for o in meshes if o.get("plynth_kind") == COLLISION_KIND]
    if not ctx.state["meshes"]:
        raise WorkerError("no_geometry", "Nothing is left to export.")


EXPORT_STEPS.register("prepare", Step(ORDER, "Preparing objects", run))
