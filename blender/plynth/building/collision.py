import bmesh
import bpy

from . import Step, count, report
from ..registry import BUILD_STEPS

ORDER = 110
COLLECTION = "Collision"
MINIMUM_VOLUME = 1e-9
MINIMUM_THICKNESS = 1e-4


# Makes the collision meshes: one closed convex mesh per piece the job lists, then one per hull (the convex hull of
# a point set, for roofs), named UCX_<object name>_<NN> with NN counted over the pieces and then the hulls, and
# one convex hull per furniture item. They go into a Collision collection and carry the kind "collision" and
# the name of the object they belong to. A piece or hull that cannot be made is reported and left out.
def run(ctx):
    bpy.context.view_layer.update()
    collection = None
    for floor in ctx.params["floors"]:
        for shape in floor.get("collision", []):
            collection = collection or collision_collection()
            make_shape(ctx, floor, shape, collection)
    items = [(floor, item) for floor in ctx.params["floors"] for item in floor["items"]]
    hulls = {}
    for floor, item in items:
        empty = bpy.data.objects.get("SM_%s_%s" % (item["asset"], item["id"]))
        if empty is None:
            continue
        collection = collection or collision_collection()
        make_item_hull(ctx, floor, item, empty, collection, hulls)


# Returns the Collision collection, made and linked to the scene on first use.
def collision_collection():
    found = bpy.data.collections.get(COLLECTION)
    if found is None:
        found = bpy.data.collections.new(COLLECTION)
        bpy.context.scene.collection.children.link(found)
    return found


# Returns the built object of a collision target (its plynth kind and id, and for a roof its index), or None.
def find_target(kind, plan_id, index=0):
    for obj in bpy.data.objects:
        if obj.get("plynth_kind") == kind and obj.get("plynth_id") == plan_id:
            if kind != "roof" or obj.get("plynth_roof_index") == index:
                return obj
    return None


# Makes every piece of one shape.
def make_shape(ctx, floor, shape, collection):
    target = find_target(shape["target"]["kind"], shape["target"]["id"], shape["index"])
    plan_id = shape["target"]["id"]
    if target is None:
        report(ctx, "collision_failed", "warning", "The %s of %s was not built, so it has no collision." % (
            shape["target"]["kind"], plan_id), floor["id"], plan_id)
        return
    sources = [(piece_mesh, piece) for piece in shape["pieces"]]
    sources += [(hull_points_mesh, hull) for hull in shape["hulls"]]
    for number, (make, source) in enumerate(sources):
        name = "UCX_%s_%02d" % (target.name, number)
        mesh = make(name, source)
        if mesh is None:
            report(ctx, "collision_failed", "warning", "A collision piece of %s could not be made." % plan_id,
                   floor["id"], plan_id)
            continue
        add_object(name, mesh, collection, target.name, target.get("plynth_id", plan_id))
        count(ctx, "collision")


# Builds the closed prism of one piece: a bottom and a top face and one face per outline edge. Returns None
# for a piece that is flat or has no area.
def piece_mesh(name, piece):
    outline = piece["outline"]
    if len(outline) < 3 or piece["top"] - piece["bottom"] < MINIMUM_THICKNESS:
        return None
    bm = bmesh.new()
    low = [bm.verts.new((p[0], p[1], piece["bottom"])) for p in outline]
    high = [bm.verts.new((p[0], p[1], piece["top"])) for p in outline]
    try:
        bm.faces.new(list(reversed(low)))
        bm.faces.new(high)
        total = len(outline)
        for i in range(total):
            j = (i + 1) % total
            bm.faces.new([low[i], low[j], high[j], high[i]])
    except ValueError:
        bm.free()
        return None
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return finish_mesh(name, bm)


# Builds the closed convex mesh of a point set (world X, Y and Z), or None when the points are flat or have no
# volume.
def hull_points_mesh(name, points):
    bm = bmesh.new()
    for point in points:
        bm.verts.new((point[0], point[1], point[2]))
    try:
        result = bmesh.ops.convex_hull(bm, input=bm.verts[:])
        drop = list({g for g in result["geom_interior"] + result["geom_unused"] if isinstance(g, bmesh.types.BMVert)})
        bmesh.ops.delete(bm, geom=drop, context="VERTS")
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    except Exception:
        bm.free()
        return None
    return finish_mesh(name, bm)


# Turns a bmesh into a mesh when it is closed and has volume, and frees it. Returns None otherwise.
def finish_mesh(name, bm):
    closed = all(len(edge.link_faces) == 2 for edge in bm.edges)
    volume = bm.calc_volume(signed=True) if closed and bm.faces else 0.0
    if not closed or volume < MINIMUM_VOLUME:
        bm.free()
        return None
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    return mesh


# Links a collision object into the collection and tags it.
def add_object(name, mesh, collection, target_name, plan_id):
    obj = bpy.data.objects.new(name, mesh)
    obj["plynth_kind"] = "collision"
    obj["plynth_id"] = plan_id
    obj["plynth_collision_for"] = target_name
    obj.display_type = "WIRE"
    collection.objects.link(obj)
    return obj


# Returns the world space vertices of every mesh of an asset collection, or the convex hull mesh of them in
# the asset's own space (the asset's collection origin), or None when there is no solid hull.
def hull_mesh(name, asset_collection):
    bm = bmesh.new()
    for source in asset_collection.all_objects:
        if source.type != "MESH":
            continue
        matrix = source.matrix_world
        for vertex in source.data.vertices:
            bm.verts.new(matrix @ vertex.co)
    if len(bm.verts) < 4:
        bm.free()
        return None
    try:
        result = bmesh.ops.convex_hull(bm, input=bm.verts[:])
        drop = list({g for g in result["geom_interior"] + result["geom_unused"] if isinstance(g, bmesh.types.BMVert)})
        bmesh.ops.delete(bm, geom=drop, context="VERTS")
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    except Exception:
        bm.free()
        return None
    return finish_mesh(name, bm)


# Makes the hull object of one item, sharing the mesh of its asset's hull, placed where the item is.
def make_item_hull(ctx, floor, item, empty, collection, hulls):
    asset = item["asset"]
    if asset not in hulls:
        source = ctx.state.get("asset_collections", {}).get(asset)
        hulls[asset] = hull_mesh("UCX_%s" % asset, source) if source is not None else None
    mesh = hulls[asset]
    if mesh is None:
        report(ctx, "collision_failed", "warning", "The furniture %s has no collision hull." % item["id"],
               floor["id"], item["id"])
        return
    obj = add_object("UCX_%s_00" % empty.name, mesh, collection, empty.name, item["id"])
    obj.matrix_world = empty.matrix_world.copy()
    count(ctx, "collision")


BUILD_STEPS.register("collision", Step(ORDER, "Adding collision", run))
