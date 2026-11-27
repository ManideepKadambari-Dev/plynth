import math

import bmesh

from . import Step, count, report, slot_name
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 35
MIN_AREA = 1e-9
WELD = 1e-8


# Lays the slab above every floor that has one: one object per floor with side faces on every ring, the
# undersides that show and the ledge tops too narrow for a roof. The rest of the top is never built because a
# floor or a roof always covers it.
def run(ctx):
    for floor in ctx.params["floors"]:
        slab = floor["slab"]
        if not slab["polygons"] and not slab["undersides"] and not slab["tops"]:
            continue
        try:
            make_slab(ctx, floor, slab)
        except Exception as error:
            report(ctx, "slab_failed", "error", "The slab of floor %s could not be built: %s" % (floor["id"], error),
                   floor["id"], floor["id"])


# Returns a ring oriented anticlockwise (outer) or clockwise (hole) seen from above. A ring with fewer than
# three points, a bad number or no area is refused.
def oriented(ring, outer):
    points = [(float(p[0]), float(p[1])) for p in ring]
    if len(points) < 3 or not all(math.isfinite(c) for p in points for c in p):
        raise ValueError("a ring has fewer than three points or a bad number")
    area = meshes.polygon_area(points)
    if abs(area) < MIN_AREA:
        raise ValueError("a ring has no area")
    return points if (area > 0) == outer else points[::-1]


# Returns the side names of a ring in the order of the ring after oriented turned it, so that names[i] stays
# the name of the edge from point i to the next. A turned ring has its edges in the opposite order.
def sides_for(ring, names, outer):
    if len(names) != len(ring):
        raise ValueError("a ring has %d points but %d side names" % (len(ring), len(names)))
    points = [(float(p[0]), float(p[1])) for p in ring]
    turned = (meshes.polygon_area(points) > 0) != outer
    if not turned:
        return list(names)
    return [names[(len(names) - 2 - i) % len(names)] for i in range(len(names))]


# Returns the side names of every ring of every polygon, as a pair (outline names, hole names) per polygon.
def side_names(slab):
    polygons = slab["polygons"]
    sides = slab["sides"]
    if len(sides) != len(polygons):
        raise ValueError("%d polygons but %d side lists" % (len(polygons), len(sides)))
    for polygon, entry in zip(polygons, sides):
        if len(entry["holes"]) != len(polygon["holes"]):
            raise ValueError("a polygon has %d holes but %d hole side lists" % (len(polygon["holes"]), len(entry["holes"])))
    return [(entry["outline"], entry["holes"]) for entry in sides]


# Adds the side faces of one ring from z0 to z1 facing away from the slab, with u along the ring and v the
# height above the ground, so brick courses run on from the walls below and above. slots holds the slot of each
# edge.
def add_sides(bm, ring, z0, z1, slots):
    uv = meshes.uv_layer(bm)
    bottom = [bm.verts.new((x, y, z0)) for x, y in ring]
    top = [bm.verts.new((x, y, z1)) for x, y in ring]
    along = 0.0
    for i, p in enumerate(ring):
        j = (i + 1) % len(ring)
        q = ring[j]
        length = math.hypot(q[0] - p[0], q[1] - p[1])
        face = bm.faces.new([bottom[i], bottom[j], top[j], top[i]])
        face.material_index = slots[i]
        for k, loop in enumerate(face.loops):
            loop[uv].uv = (along if k in (0, 3) else along + length, z0 if k < 2 else z1)
        along += length


# Returns triangles (index triples) that fill an anticlockwise ring without holes, by cutting off one ear at a time.
# It only uses the sign of cross products in double precision, so a ledge a micrometre wide still fills.
def ear_clip(ring):
    order = list(range(len(ring)))
    triangles = []

    def cross(a, b, c):
        return ((ring[b][0] - ring[a][0]) * (ring[c][1] - ring[a][1])
                - (ring[b][1] - ring[a][1]) * (ring[c][0] - ring[a][0]))

    def inside(p, a, b, c):
        return cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0

    while len(order) > 3:
        for k in range(len(order)):
            a, b, c = order[k - 1], order[k], order[(k + 1) % len(order)]
            if cross(a, b, c) <= 0:
                continue
            if any(inside(p, a, b, c) for p in order if p not in (a, b, c)):
                continue
            triangles.append((a, b, c))
            del order[k]
            break
        else:
            raise ValueError("a ledge ring could not be filled")
    triangles.append(tuple(order))
    return triangles


# Adds a face at height z for a ledge top (up) or an overhang underside (down) and returns the faces. Both can be
# slivers that the usual triangulation drops or doubles up, so its answer is checked against the area and, for a
# polygon without holes, replaced by ear clipping.
def add_ledge(bm, outline, holes, z, up=True):
    # Measured from the outline's first point, so points far from the origin stay apart in float32. The
    # vertices keep their own places.
    ox, oy = outline[0][0], outline[0][1]
    near = [[[p[0] - ox, p[1] - oy] for p in ring] for ring in [outline] + list(holes)]
    triangles = meshes.triangulate(near[0], near[1:])
    points = outline + [p for hole in holes for p in hole]
    flat = [p for ring in near for p in ring]
    wanted = meshes.polygon_area(near[0]) + sum(meshes.polygon_area(h) for h in near[1:])
    got = sum(abs(meshes.polygon_area([flat[i] for i in t])) for t in triangles)
    if any(len(set(t)) < 3 for t in triangles) or abs(got - wanted) > 1e-3 * wanted:
        if holes:
            raise ValueError("a ledge with holes could not be filled")
        triangles = ear_clip(near[0])
    if not up:
        triangles = [(a, c, b) for a, b, c in triangles]
    verts = [bm.verts.new((p[0], p[1], z)) for p in points]
    return [bm.faces.new([verts[i] for i in t]) for t in triangles]


# Returns the slot index of a material name, adding the name to used when it is new, so an object only gets
# the materials its faces use and two parts with the same material share a slot.
def slot(used, name):
    if name not in used:
        used.append(name)
    return used.index(name)


# Builds the slab object of one floor.
def make_slab(ctx, floor, slab):
    slots = slab["slots"]
    names = [slot_name(ctx, slots["edge"]), slot_name(ctx, slots["well"]), slot_name(ctx, slots["underside"])]
    kinds = {"edge": names[0], "well": names[1]}
    used = []
    bm = bmesh.new()
    try:
        for polygon, (outline_names, hole_names) in zip(slab["polygons"], side_names(slab)):
            ring = oriented(polygon["outline"], True)
            slots = [slot(used, kinds[name]) for name in sides_for(polygon["outline"], outline_names, True)]
            add_sides(bm, ring, slab["bottom"], slab["top"], slots)
            for hole, hole_sides in zip(polygon["holes"], hole_names):
                ring = oriented(hole, False)
                slots = [slot(used, kinds[name]) for name in sides_for(hole, hole_sides, False)]
                add_sides(bm, ring, slab["bottom"], slab["top"], slots)
        for polygon in slab["undersides"]:
            outline = oriented(polygon["outline"], True)
            holes = [oriented(hole, False) for hole in polygon["holes"]]
            faces = add_ledge(bm, outline, holes, slab["bottom"], False)
            meshes.world_uvs(bm, faces, flip_y=True)
            for face in faces:
                face.material_index = slot(used, names[2])
        for polygon in slab["tops"]:
            outline = oriented(polygon["outline"], True)
            holes = [oriented(hole, False) for hole in polygon["holes"]]
            faces = add_ledge(bm, outline, holes, slab["top"])
            meshes.world_uvs(bm, faces)
            for face in faces:
                face.material_index = slot(used, names[0])
        bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=WELD)
        collection = ctx.state["collections"][floor["id"]]
        obj = meshes.make_object("%s_Slab" % floor["id"], bm, collection, floor["id"], "slab")
    except Exception:
        bm.free()
        raise
    meshes.add_slots(obj, used)
    count(ctx, "slabs")


BUILD_STEPS.register("slabs", Step(ORDER, "Laying floor slabs", run))
