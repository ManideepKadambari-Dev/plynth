import contextlib
import sys
from collections import namedtuple

import addon_utils
import bmesh
import bpy
import mathutils

from .. import extensions, registry
from ..generating import AddonUnavailable

# What each bridge registers: the add-on short name, the short names its module may have, and the object that
# holds its calls (it also has available()).
Bridge = namedtuple("Bridge", "addon modules api")

DATA_KINDS = ("meshes", "curves", "materials", "node_groups", "collections", "images", "lights", "cameras")


# Returns the module name of the installed add-on whose last dotted part is one of the short names, or None.
def find_module(short_names):
    wanted = [name.lower() for name in short_names]
    for module in addon_utils.modules(refresh=False):
        if module.__name__.split(".")[-1].lower() in wanted:
            return module.__name__
    return None


# Turns an installed add-on on for this Blender session and returns true when it is on. The add-on is
# registered through the preferences list of this session only, and a background run never saves it. Without
# that registration some add-ons (Bool Tool, Bagapie) fail to switch on. It refuses unless Blender's extensions
# folder was prepared for this run, so the user's shared folder is never touched.
def enable_addon(short_names):
    if not extensions.is_private():
        return False
    name = find_module(short_names)
    if name is None:
        return False
    try:
        if addon_utils.check(name)[1]:
            return True
        return addon_utils.enable(name, default_set=True, persistent=False) is not None
    except Exception:
        return False


# Returns the registered bridge for an add-on short name with the add-on enabled. Raises AddonUnavailable.
def get(short_name):
    registry.discover(sys.modules[__name__])
    bridge = registry.ADDON_BRIDGES.get(short_name)
    if bridge is None:
        raise AddonUnavailable("There is no bridge for the add-on %s." % short_name)
    if not enable_addon(bridge.modules):
        raise AddonUnavailable("The add-on %s is not installed or cannot be enabled." % bridge.addon)
    return bridge


# Registers a bridge whose api object has an available() check that enables the add-on.
def register(addon, modules, api):
    return registry.ADDON_BRIDGES.register(addon, Bridge(addon, tuple(modules), api))


# Remembers which objects and data blocks exist, so what a call made can be found and removed again.
class Scope:
    def __init__(self):
        self.objects = set(bpy.data.objects)
        self.data = {kind: set(getattr(bpy.data, kind)) for kind in DATA_KINDS}

    # Returns the objects made since the scope began.
    def new_objects(self):
        return [obj for obj in bpy.data.objects if obj not in self.objects]

    # Removes every object made since the scope began, except the ones to keep, then the data left unused.
    def clean(self, keep=()):
        keep = set(keep)
        for obj in self.new_objects():
            if obj not in keep:
                bpy.data.objects.remove(obj, do_unlink=True)
        for kind in DATA_KINDS:
            collection = getattr(bpy.data, kind)
            for block in list(collection):
                if block in self.data[kind] or block.users:
                    continue
                if kind == "collections" and (block.objects or block.children):
                    continue
                collection.remove(block)
        changed = True
        while changed:
            changed = False
            for collection in list(bpy.data.collections):
                if collection not in self.data["collections"] and not collection.objects and not collection.children:
                    bpy.data.collections.remove(collection)
                    changed = True


# Puts the 3D cursor at the origin while a call runs, because add-on operators place objects at the cursor.
@contextlib.contextmanager
def cursor_at_origin():
    cursor = bpy.context.scene.cursor
    saved = tuple(cursor.location), tuple(cursor.rotation_euler)
    cursor.location = (0.0, 0.0, 0.0)
    cursor.rotation_euler = (0.0, 0.0, 0.0)
    try:
        yield
    finally:
        cursor.location, cursor.rotation_euler = saved


# Makes the objects the only selected ones, with the first as the active one, in object mode.
def select_only(objects):
    if bpy.context.object is not None and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


# Joins the evaluated geometry of the objects (modifiers, geometry nodes and drivers made real, world
# transforms applied) into one new mesh object linked to the scene. Material slots are kept.
def realize(objects, name):
    bpy.context.view_layer.update()
    graph = bpy.context.evaluated_depsgraph_get()
    combined = bmesh.new()
    materials = []
    for obj in objects:
        evaluated = obj.evaluated_get(graph)
        mesh = bpy.data.meshes.new_from_object(evaluated)
        if mesh is None or not len(mesh.vertices):
            continue
        mesh.transform(obj.matrix_world)
        first = len(combined.faces)
        combined.from_mesh(mesh)
        combined.faces.ensure_lookup_table()
        slots = [material for material in mesh.materials]
        for face in combined.faces[first:]:
            material = slots[face.material_index] if face.material_index < len(slots) else None
            if material not in materials:
                materials.append(material)
            face.material_index = materials.index(material)
        bpy.data.meshes.remove(mesh)
    mesh = bpy.data.meshes.new(name)
    combined.to_mesh(mesh)
    combined.free()
    for material in materials:
        mesh.materials.append(material)
    result = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(result)
    return result


# Moves a mesh object so its bounds are centred on x and y and its lowest point is at z 0, then bakes the move
# into the vertices so the object has an identity transform.
def stand_on_origin(obj):
    points = [vertex.co for vertex in obj.data.vertices]
    if not points:
        return obj
    low = [min(point[axis] for point in points) for axis in range(3)]
    high = [max(point[axis] for point in points) for axis in range(3)]
    obj.data.transform(mathutils.Matrix.Translation((-(low[0] + high[0]) / 2, -(low[1] + high[1]) / 2, -low[2])))
    return obj


# Returns the size (x, y, z) of the bounds of the vertices of a mesh object.
def mesh_size(obj):
    points = [vertex.co for vertex in obj.data.vertices]
    if not points:
        return (0.0, 0.0, 0.0)
    return tuple(max(p[axis] for p in points) - min(p[axis] for p in points) for axis in range(3))


# Fills every open boundary of a mesh object with a flat face, so the mesh is closed, and makes the normals
# point outward. Returns the object.
def close_holes(obj):
    builder = bmesh.new()
    builder.from_mesh(obj.data)
    open_edges = [edge for edge in builder.edges if len(edge.link_faces) == 1]
    if open_edges:
        bmesh.ops.holes_fill(builder, edges=open_edges, sides=len(open_edges))
        bmesh.ops.recalc_face_normals(builder, faces=builder.faces)
        builder.to_mesh(obj.data)
    builder.free()
    return obj
