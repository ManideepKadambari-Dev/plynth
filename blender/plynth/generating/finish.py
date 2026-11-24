import bmesh
import bpy
from mathutils import Matrix

from .. import meshes, surfaces

GENERATED = "Generated"
DATA_KINDS = ("objects", "meshes", "curves", "materials", "node_groups", "images", "collections", "fonts",
              "lights", "cameras", "metaballs", "textures", "worlds")


# Remembers every data block that exists now, so what a generator adds can be told apart.
def snapshot():
    return {kind: {block.as_pointer() for block in getattr(bpy.data, kind)} for kind in DATA_KINDS}


# Returns the mesh of an object with its modifiers applied and its world matrix baked in, as a new mesh.
def real_mesh(obj):
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))
    mesh.transform(obj.matrix_world)
    return mesh


# Joins the meshes of the objects into one bmesh in the item's own space. Returns it with the list of materials
# its faces use: the ones the generator took from gen.material, and the default for any other.
def join(gen, objects, default):
    bm = bmesh.new()
    layer = meshes.uv_layer(bm)
    wanted = []
    for obj in objects:
        mesh = real_mesh(obj)
        source = mesh.uv_layers.get("UVMap") or (mesh.uv_layers[0] if mesh.uv_layers else None)
        verts = [bm.verts.new(v.co) for v in mesh.vertices]
        for polygon in mesh.polygons:
            try:
                face = bm.faces.new([verts[i] for i in polygon.vertices])
            except ValueError:
                continue
            mat = mesh.materials[polygon.material_index] if polygon.material_index < len(mesh.materials) else None
            wanted.append(mat if mat in gen.materials.values() else default)
            face.smooth = polygon.use_smooth
            face.normal_update()
            if gen.keep_uvs and source is not None:
                for loop, index in zip(face.loops, polygon.loop_indices):
                    loop[layer].uv = source.data[index].uv
            else:
                meshes.project_face(face, layer)
        bpy.data.meshes.remove(mesh)
    kept = []
    for mat in wanted:
        if mat not in kept:
            kept.append(mat)
    bm.faces.ensure_lookup_table()
    for face, mat in zip(bm.faces, wanted):
        face.material_index = kept.index(mat)
    bm.normal_update()
    return bm, kept


# Returns the sides by which the mesh reaches past its size, as text, or an empty string.
def size_overshoot(bm, size, tolerance):
    if not bm.verts:
        return ""
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    width, depth, height = size
    reach = {
        "left": -width / 2 - min(xs), "right": max(xs) - width / 2,
        "front": -depth / 2 - min(ys), "back": max(ys) - depth / 2,
        "bottom": -min(zs), "top": max(zs) - height,
    }
    return ", ".join("%s by %.0f mm" % (side, value * 1000) for side, value in reach.items() if value > tolerance)


# Makes the item's final object from what the generator returned and puts it in the floor collection at its
# place. Returns the object and the size overshoot text.
def finish(ctx, gen, objects, item, floor_collection, tolerance):
    mesh_objects = [o for o in objects if o is not None]
    if not mesh_objects:
        return None, ""
    first_material = next((p for p in gen._declared.values() if p["type"] == "material"), None)
    if first_material is not None and first_material["id"] not in gen.materials:
        gen.material(first_material["id"])
    default = (gen.materials[first_material["id"]] if first_material is not None
               else surfaces.material(GENERATED))
    bm, kept = join(gen, mesh_objects, default)
    if not bm.faces:
        bm.free()
        return None, ""
    overshoot = size_overshoot(bm, gen.size, tolerance)
    name = "SM_%s_%s" % (gen.generator, item["id"])
    obj = meshes.make_object(name, bm, floor_collection, item["id"], "generator")
    for mat in kept:
        obj.data.materials.append(mat)
    obj["plynth_generator"] = gen.generator
    obj.location = tuple(item["location"])
    obj.rotation_euler = (0.0, 0.0, item["rotation_z"])
    return obj, overshoot


# Removes everything the generator made that is not the kept object: its objects, then the data blocks nothing
# uses any more.
def clean(before, keep):
    keep_pointers = {keep.as_pointer(), keep.data.as_pointer()} if keep is not None else set()
    for obj in [o for o in bpy.data.objects if o.as_pointer() not in before["objects"]]:
        if obj.as_pointer() not in keep_pointers:
            bpy.data.objects.remove(obj, do_unlink=True)
    for kind in DATA_KINDS:
        if kind == "objects":
            continue
        collection = getattr(bpy.data, kind)
        for block in [b for b in collection if b.as_pointer() not in before[kind]]:
            if block.as_pointer() in keep_pointers:
                continue
            if kind == "materials" and block.users > 0:
                continue
            if kind == "images" and block.users > 0:
                continue
            collection.remove(block)

