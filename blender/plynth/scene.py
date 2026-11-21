import bpy
from mathutils import Matrix, Vector

CONVERTIBLE_TYPES = {"CURVE", "SURFACE", "FONT", "META"}


# Removes every object, collection and unused data block, so a model can be imported into an empty scene.
def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    bpy.data.orphans_purge(do_recursive=True)


# Returns the objects of the scene, including those in nested collections.
def scene_objects():
    return list(bpy.context.scene.collection.all_objects)


# Returns the mesh objects among the given objects.
def mesh_objects(objects):
    return [obj for obj in objects if obj.type == "MESH"]


# Returns the world space minimum and maximum corners of the mesh objects, or None when there are none.
def world_bounds(objects):
    low = [float("inf")] * 3
    high = [float("-inf")] * 3
    found = False
    for obj in mesh_objects(objects):
        matrix = obj.matrix_world
        for corner in obj.bound_box:
            point = matrix @ Vector(corner)
            for axis in range(3):
                low[axis] = min(low[axis], point[axis])
                high[axis] = max(high[axis], point[axis])
            found = True
    if not found:
        return None
    return Vector(low), Vector(high)


# Returns the size of the world bounds as (width, depth, height), or None without geometry.
def world_size(objects):
    bounds = world_bounds(objects)
    if bounds is None:
        return None
    size = bounds[1] - bounds[0]
    return size.x, size.y, size.z


# Counts the triangles the mesh objects would have once every face is triangulated.
def triangle_count(objects):
    total = 0
    for obj in mesh_objects(objects):
        mesh = obj.data
        mesh.calc_loop_triangles()
        total += len(mesh.loop_triangles)
    return total


# Detaches every object from its parent without moving it.
def unparent(objects):
    for obj in objects:
        if obj.parent is not None:
            world = obj.matrix_world.copy()
            obj.parent = None
            obj.matrix_world = world


# Replaces the mesh of each object that has modifiers by the result of its modifiers, then drops them.
def bake_modifiers(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in mesh_objects(objects):
        if not obj.modifiers:
            continue
        evaluated = obj.evaluated_get(depsgraph)
        baked = bpy.data.meshes.new_from_object(evaluated)
        old = obj.data
        obj.shape_key_clear()
        obj.modifiers.clear()
        obj.data = baked
        if old.users == 0:
            bpy.data.meshes.remove(old)


# Moves each mesh object's transform into its vertices so every matrix is the identity. Meshes shared
# between objects are copied first.
def apply_object_transforms(objects):
    for obj in mesh_objects(objects):
        if obj.data.users > 1:
            obj.data = obj.data.copy()
        matrix = obj.matrix_world.copy()
        obj.data.transform(matrix)
        if matrix.determinant() < 0:
            obj.data.flip_normals()
        obj.matrix_world = Matrix.Identity(4)
        obj.data.update()


# Converts curves, text and similar objects to meshes. Returns the objects that could not be converted.
def convert_to_meshes(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    failed = []
    for obj in objects:
        if obj.type not in CONVERTIBLE_TYPES:
            continue
        try:
            mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))
        except Exception:
            mesh = None
        if mesh is None or len(mesh.vertices) == 0:
            failed.append(obj)
            continue
        replacement = bpy.data.objects.new(obj.name, mesh)
        replacement.matrix_world = obj.matrix_world.copy()
        for collection in obj.users_collection:
            collection.objects.link(replacement)
        bpy.data.objects.remove(obj, do_unlink=True)
    return failed
