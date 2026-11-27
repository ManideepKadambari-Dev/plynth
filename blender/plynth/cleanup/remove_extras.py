import bpy
from mathutils import Matrix

from . import Step
from .. import scene
from ..jobs import WorkerError
from ..registry import CLEANUP_STEPS

ORDER = 20
REMOVED_KINDS = {"CAMERA": "camera", "LIGHT": "light", "EMPTY": "empty"}
PLURALS = {"camera": "cameras", "light": "lights", "empty": "empties", "empty mesh": "empty meshes",
           "hidden object": "hidden objects"}
# How far a matrix entry may be from the identity's before the rest of a transform counts as a slant.
SLANT_TOLERANCE = 1e-5


# Turns curves and text into meshes, bakes modifiers and shape keys, and removes cameras, lights, empties,
# objects hidden from render and anything that is not a mesh. Objects keep their place when their parent goes.
# Raises no_geometry when no mesh is left.
def run(ctx):
    objects = scene.scene_objects()
    for obj in objects:
        obj.hide_viewport = False
    bpy.context.view_layer.update()
    detach_in_place(objects)
    bpy.context.view_layer.update()
    scene.convert_to_meshes(scene.scene_objects())
    scene.bake_modifiers(scene.scene_objects())
    bake_shape_keys(scene.scene_objects())

    counts = {}
    hidden = remove_hidden()
    if hidden:
        counts["hidden object"] = hidden
    other = []
    for obj in scene.scene_objects():
        keep = obj.type == "MESH" and len(obj.data.vertices) > 0
        if keep:
            continue
        if obj.type in REMOVED_KINDS:
            counts[REMOVED_KINDS[obj.type]] = counts.get(REMOVED_KINDS[obj.type], 0) + 1
        elif obj.type == "MESH":
            counts["empty mesh"] = counts.get("empty mesh", 0) + 1
        else:
            other.append("%s (%s)" % (obj.name, obj.type.lower()))
        bpy.data.objects.remove(obj, do_unlink=True)

    if counts:
        parts = ["%d %s" % (count, name if count == 1 else PLURALS[name]) for name, count in sorted(counts.items())]
        ctx.warn("removed_objects", "Removed " + ", ".join(parts) + ".")
    if other:
        ctx.warn("unsupported_object", "Removed objects that cannot be used: " + ", ".join(sorted(other)) + ".")
    if not scene.mesh_objects(scene.scene_objects()):
        raise WorkerError("no_geometry", "The file holds no model with geometry.")


# Fixes every object where the file shows it and detaches it from its parent. The places are read first, while
# parents, animation, drivers and constraints still act. Then animation, drivers and constraints are removed,
# since they would move an object again once its parent or their target is gone: a keyed turn would undo the
# turn of a removed parent and lay the model on its side.
def detach_in_place(objects):
    places = [(obj, obj.matrix_world.copy()) for obj in objects]
    for obj, _world in places:
        obj.animation_data_clear()
        obj.constraints.clear()
    for obj, world in places:
        obj.parent = None
        keep_world(obj, world)


# Puts an object at a place in the world. The slant that a turned child of an unevenly scaled parent can have
# does not fit in an object's own position, turn and scale, so that part goes into its mesh.
def keep_world(obj, world):
    location, rotation, scale = world.decompose()
    plain = Matrix.LocRotScale(location, rotation, scale)
    rest = plain.inverted_safe() @ world
    if obj.type == "MESH" and not is_identity(rest):
        if obj.data.users > 1:
            obj.data = obj.data.copy()
        obj.data.transform(rest, shape_keys=True)
        obj.data.update()
        world = plain
    obj.matrix_world = world


# Says whether a matrix is the identity within a small tolerance.
def is_identity(matrix):
    unit = Matrix.Identity(4)
    return all(abs(matrix[row][col] - unit[row][col]) <= SLANT_TOLERANCE for row in range(4) for col in range(4))


# Replaces the mesh of each object that still has shape keys by the shape they give it now and drops the keys,
# so moving the mesh later moves what is shown. The shapes are all read before any key is dropped, since
# objects may share a mesh.
def bake_shape_keys(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    keyed = [obj for obj in scene.mesh_objects(objects) if obj.data.shape_keys is not None]
    baked = [(obj, bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))) for obj in keyed]
    for obj, mesh in baked:
        old = obj.data
        if old.shape_keys is not None:
            obj.shape_key_clear()
        obj.data = mesh
        if old.users == 0:
            bpy.data.meshes.remove(old)


# Removes the mesh objects hidden from render, such as helpers and spare variants the file keeps, and returns
# how many went. When every mesh is hidden none is removed, so a file saved with all hidden still imports.
def remove_hidden():
    meshes = [obj for obj in scene.scene_objects() if obj.type == "MESH" and len(obj.data.vertices) > 0]
    hidden = [obj for obj in meshes if obj.hide_render]
    if len(hidden) == len(meshes):
        for obj in hidden:
            obj.hide_render = False
        return 0
    for obj in hidden:
        bpy.data.objects.remove(obj, do_unlink=True)
    return len(hidden)


CLEANUP_STEPS.register("remove_extras", Step(ORDER, "Removing cameras, lights and extras", run))
