import math

import bmesh
import bpy
import numpy as np

from . import Step, count, report
from ..quality import collision as quality_collision
from ..registry import BUILD_STEPS

ORDER = 110
COLLECTION = "Collision"
MINIMUM_VOLUME = 1e-9
MINIMUM_THICKNESS = 1e-4
MAX_HULL_POINTS = 64
WELD_RATIO = 1e-4
PRUNE_RATIO = 2e-3
PRUNE_MINIMUM = 1e-3
CHECK_MARGIN = 0.25
HULL_EPSILON = 1e-7


# Makes the collision meshes: one closed convex mesh per piece the job lists, then one per hull (the convex hull of
# a point set, for roofs), named UCX_<object name>_<NN> with NN counted over the pieces and then the hulls, and
# one convex hull per furniture item and per built generator item. They go into a Collision collection and
# carry the kind "collision" and the name of the object they belong to. A piece or hull that cannot be made is
# reported and left out. A generator item that was not built has no hull and no warning.
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
    for floor in ctx.params["floors"]:
        for item in floor.get("generator_items", []):
            target = bpy.data.objects.get("SM_%s_%s" % (item["generator"], item["id"]))
            if target is None or target.get("plynth_kind") != "generator":
                continue
            collection = collection or collision_collection()
            make_generator_hull(ctx, floor, item, target, collection)


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
# volume. A dense set is first cut down to its outermost points in a fixed set of directions, so the hull stays
# small enough for a game engine, and points that barely change the hull are dropped, so it has no sliver faces
# whose direction is lost to rounding. The hull is built here rather than with the bmesh convex hull, which can
# leave dents when many points lie almost in one plane. A hull that cannot be made, or that the export's
# convexity check could still reject, is replaced by the smaller of the axis box and the oriented box round the
# points.
def hull_points_mesh(name, points):
    cloud = np.array([tuple(p)[:3] for p in points], dtype=np.float64).reshape(-1, 3)
    if len(cloud) < 4:
        return None
    mesh = convex_mesh(name, prune_points(outer_points(cloud)))
    if mesh is not None and quality_collision.is_convex(mesh, quality_collision.TOLERANCE * CHECK_MARGIN):
        return mesh
    if mesh is not None:
        bpy.data.meshes.remove(mesh)
    return convex_mesh(name, box_corners(cloud))


# Returns the outward unit normal and offset of the plane through three points, or None for a sliver.
def face_plane(a, b, c):
    normal = np.cross(b - a, c - a)
    length = float(np.linalg.norm(normal))
    if length < 1e-12:
        return None
    normal = normal / length
    return normal, float(normal @ a)


# Returns the convex hull of the points as triangles of point indices, each running anticlockwise seen from
# outside, or None when the points are flat. Points are added one at a time, the farthest out first: each one
# that lies more than HULL_EPSILON outside some faces replaces those faces by a fan from their outline to the
# point. A point that
# would make a fan face with no area is skipped, as it lies on the hull already.
def hull_triangles(cloud):
    if len(cloud) < 4:
        return None
    first = int(np.argmin(cloud[:, 0]))
    offsets = cloud - cloud[first]
    second = int(np.argmax(np.linalg.norm(offsets, axis=1)))
    length = float(np.linalg.norm(offsets[second]))
    if length < HULL_EPSILON:
        return None
    line = offsets[second] / length
    across = offsets - np.outer(offsets @ line, line)
    third = int(np.argmax(np.linalg.norm(across, axis=1)))
    if np.linalg.norm(across[third]) < HULL_EPSILON:
        return None
    base = face_plane(cloud[first], cloud[second], cloud[third])
    heights = offsets @ base[0]
    fourth = int(np.argmax(np.abs(heights)))
    if abs(heights[fourth]) < HULL_EPSILON:
        return None
    start = (first, second, third, fourth)
    inside = cloud[list(start)].mean(axis=0)
    faces = {}
    for a, b, c in ((first, second, third), (first, second, fourth), (first, third, fourth),
                    (second, third, fourth)):
        normal, offset = face_plane(cloud[a], cloud[b], cloud[c])
        if normal @ inside - offset > 0.0:
            faces[(a, c, b)] = (-normal, -offset)
        else:
            faces[(a, b, c)] = (normal, offset)
    owner = {edge: t for t in faces for edge in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0]))}
    order = np.argsort(-np.linalg.norm(cloud - inside, axis=1), kind="stable")
    for index in (int(i) for i in order):
        if index in start:
            continue
        point = cloud[index]
        height = {t: float(normal @ point - offset) for t, (normal, offset) in faces.items()}
        top = max(height, key=height.get)
        if height[top] <= HULL_EPSILON:
            continue
        # The faces the point sees are gathered by walking from the one it sees best, so they form one patch
        # with one outline even where rounding leaves a far face barely seen.
        seen = {top}
        waiting = [top]
        while waiting:
            a, b, c = waiting.pop()
            for edge in ((b, a), (c, b), (a, c)):
                other = owner[edge]
                if other not in seen and height[other] > HULL_EPSILON:
                    seen.add(other)
                    waiting.append(other)
        edges = {e for a, b, c in seen for e in ((a, b), (b, c), (c, a))}
        fan = {(a, b, index): face_plane(cloud[a], cloud[b], point) for a, b in edges if (b, a) not in edges}
        if any(plane is None for plane in fan.values()):
            continue
        for triangle in seen:
            del faces[triangle]
            for edge in ((triangle[0], triangle[1]), (triangle[1], triangle[2]), (triangle[2], triangle[0])):
                owner.pop(edge, None)
        for triangle in fan:
            for edge in ((triangle[0], triangle[1]), (triangle[1], triangle[2]), (triangle[2], triangle[0])):
                owner[edge] = triangle
        faces.update(fan)
    return list(faces)


# Returns how far a point lies outside the hull of the other points, or infinity when they have no hull.
def distance_outside(points, point):
    triangles = hull_triangles(points)
    if triangles is None:
        return math.inf
    farthest = -math.inf
    for a, b, c in triangles:
        plane = face_plane(points[a], points[b], points[c])
        if plane is not None:
            farthest = max(farthest, float(plane[0] @ point - plane[1]))
    return farthest


# Drops, round by round, the hull points that stick out of the hull of the others by less than the prune
# distance, never two neighbours in one round. Returns the points that are left.
def prune_points(cloud):
    size = float(np.linalg.norm(cloud.max(axis=0) - cloud.min(axis=0)))
    limit = max(size * PRUNE_RATIO, PRUNE_MINIMUM)
    for _ in range(MAX_HULL_POINTS):
        triangles = hull_triangles(cloud)
        if triangles is None:
            return cloud
        used = sorted({corner for triangle in triangles for corner in triangle})
        remap = {old: new for new, old in enumerate(used)}
        cloud = cloud[used]
        if len(cloud) <= 4:
            return cloud
        neighbours = {i: set() for i in range(len(cloud))}
        for triangle in triangles:
            for corner in triangle:
                neighbours[remap[corner]].update(remap[other] for other in triangle)
        small = []
        for i in range(len(cloud)):
            gap = distance_outside(np.delete(cloud, i, axis=0), cloud[i])
            if gap < limit:
                small.append((gap, i))
        dropped = set()
        for _, i in sorted(small):
            if not neighbours[i] & dropped and len(cloud) - len(dropped) > 4:
                dropped.add(i)
        if not dropped:
            return cloud
        cloud = np.delete(cloud, sorted(dropped), axis=0)
    return cloud


# Returns unit directions spread over the sphere: the 26 of a box with its edges and corners cut off, then
# evenly spread ones up to MAX_HULL_POINTS.
def hull_directions():
    axes = [(x, y, z) for x in (-1, 0, 1) for y in (-1, 0, 1) for z in (-1, 0, 1) if (x, y, z) != (0, 0, 0)]
    directions = [np.array(a, dtype=np.float64) / np.linalg.norm(a) for a in axes]
    extra = MAX_HULL_POINTS - len(directions)
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(extra):
        z = 1.0 - 2.0 * (i + 0.5) / extra
        radius = math.sqrt(max(0.0, 1.0 - z * z))
        directions.append(np.array((radius * math.cos(golden * i), radius * math.sin(golden * i), z)))
    return np.array(directions)


# Returns the points that the hull is built from: all of them when there are few, otherwise the point that
# reaches furthest in each hull direction. Points closer than the weld distance count once.
def outer_points(cloud):
    size = float(np.linalg.norm(cloud.max(axis=0) - cloud.min(axis=0)))
    weld = max(size * WELD_RATIO, 1e-6)
    _, keep = np.unique(np.round(cloud / weld), axis=0, return_index=True)
    cloud = cloud[np.sort(keep)]
    if len(cloud) <= MAX_HULL_POINTS:
        return cloud
    chosen = np.unique(np.argmax(cloud @ hull_directions().T, axis=0))
    return cloud[chosen]


# Returns the eight corners of the smaller of two boxes round the points: one along the world axes and one
# along the main directions of the points.
def box_corners(cloud):
    centre = cloud.mean(axis=0)
    _, _, main = np.linalg.svd(cloud - centre, full_matrices=False)
    best = None
    for axes in (np.eye(3), main if main.shape[0] == 3 else np.eye(3)):
        local = (cloud - centre) @ axes.T
        low, high = local.min(axis=0), local.max(axis=0)
        volume = float(np.prod(high - low))
        if best is None or volume < best[0]:
            corners = [(x, y, z) for x in (low[0], high[0]) for y in (low[1], high[1]) for z in (low[2], high[2])]
            best = (volume, np.array(corners) @ axes + centre)
    return best[1]


# Builds the closed convex hull mesh of the points, in triangles facing out, or None when it is flat, has no
# volume or is not a clean closed surface (a closed triangle surface without holes has two faces per vertex,
# less four).
def convex_mesh(name, cloud):
    triangles = hull_triangles(cloud)
    if triangles is None:
        return None
    used = sorted({corner for triangle in triangles for corner in triangle})
    if len(triangles) != 2 * len(used) - 4:
        return None
    bm = bmesh.new()
    verts = {}
    for i in used:
        verts[i] = bm.verts.new((float(cloud[i][0]), float(cloud[i][1]), float(cloud[i][2])))
    try:
        for a, b, c in triangles:
            bm.faces.new((verts[a], verts[b], verts[c]))
    except ValueError:
        bm.free()
        return None
    bm.normal_update()
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
    obj.hide_render = True
    collection.objects.link(obj)
    return obj


# Returns the convex hull mesh of every mesh of an asset collection in the asset's own space (the asset's
# collection origin), or None when there is no solid hull.
def hull_mesh(name, asset_collection):
    parts = []
    for source in asset_collection.all_objects:
        if source.type != "MESH" or len(source.data.vertices) == 0:
            continue
        local = np.empty(len(source.data.vertices) * 3, dtype=np.float64)
        source.data.vertices.foreach_get("co", local)
        local = local.reshape(-1, 3)
        matrix = np.array(source.matrix_world)
        parts.append(local @ matrix[:3, :3].T + matrix[:3, 3])
    if not parts:
        return None
    return hull_points_mesh(name, np.concatenate(parts))


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


# Makes the hull object of one generator item from the convex hull of its own mesh, placed where the item is.
# A result that is flat or has no volume gets a warning and no hull.
def make_generator_hull(ctx, floor, item, target, collection):
    name = "UCX_%s_00" % target.name
    mesh = None
    if len(target.data.vertices) >= 4:
        mesh = hull_points_mesh(name, [tuple(v.co) for v in target.data.vertices])
    if mesh is None:
        report(ctx, "collision_failed", "warning", "The generator item %s has no collision hull." % item["id"],
               floor["id"], item["id"])
        return
    obj = add_object(name, mesh, collection, target.name, item["id"])
    obj.matrix_world = target.matrix_world.copy()
    count(ctx, "collision")


BUILD_STEPS.register("collision", Step(ORDER, "Adding collision", run))
