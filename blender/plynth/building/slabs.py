import math

import bmesh

from . import Step, count, report, slot_name
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 35
MIN_AREA = 1e-9


# Lays the slab above every floor that has one: one object per floor with side faces on every ring and the
# undersides that show. The top is never built because a floor or a roof always covers it.
def run(ctx):
    for floor in ctx.params["floors"]:
        slab = floor["slab"]
        if not slab["polygons"] and not slab["undersides"]:
            continue
        try:
            make_slab(ctx, floor, slab)
        except Exception as error:
            report(ctx, "slab_failed", "warning", "The slab of floor %s could not be built: %s" % (floor["id"], error),
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


# Adds the side faces of one ring from z0 to z1 facing away from the slab, with u along the ring and v up.
def add_sides(bm, ring, z0, z1, slot):
    uv = meshes.uv_layer(bm)
    bottom = [bm.verts.new((x, y, z0)) for x, y in ring]
    top = [bm.verts.new((x, y, z1)) for x, y in ring]
    along = 0.0
    for i, p in enumerate(ring):
        j = (i + 1) % len(ring)
        q = ring[j]
        length = math.hypot(q[0] - p[0], q[1] - p[1])
        face = bm.faces.new([bottom[i], bottom[j], top[j], top[i]])
        face.material_index = slot
        for k, loop in enumerate(face.loops):
            loop[uv].uv = (along if k in (0, 3) else along + length, 0.0 if k < 2 else z1 - z0)
        along += length


# Returns the slot index of a material name, adding the name to used when it is new, so an object only gets
# the materials its faces use and two parts with the same material share a slot.
def slot(used, name):
    if name not in used:
        used.append(name)
    return used.index(name)


# Builds the slab object of one floor.
def make_slab(ctx, floor, slab):
    materials = slab["materials"]
    names = [slot_name(ctx, materials["edge"], "Wall_Outside"), slot_name(ctx, materials["well"], "Wall_Inside"),
             slot_name(ctx, materials["underside"], "Soffit")]
    used = []
    bm = bmesh.new()
    try:
        for polygon in slab["polygons"]:
            add_sides(bm, oriented(polygon["outline"], True), slab["bottom"], slab["top"], slot(used, names[0]))
            for hole in polygon["holes"]:
                add_sides(bm, oriented(hole, False), slab["bottom"], slab["top"], slot(used, names[1]))
        for polygon in slab["undersides"]:
            outline = oriented(polygon["outline"], True)
            holes = [oriented(hole, False) for hole in polygon["holes"]]
            faces = meshes.add_flat(bm, outline, holes, slab["bottom"], False)
            meshes.world_uvs(bm, faces, flip_y=True)
            for face in faces:
                face.material_index = slot(used, names[2])
        collection = ctx.state["collections"][floor["id"]]
        obj = meshes.make_object("SM_%s_Slab" % floor["id"], bm, collection, floor["id"], "slab")
    except Exception:
        bm.free()
        raise
    meshes.add_slots(obj, used)
    count(ctx, "slabs")


BUILD_STEPS.register("slabs", Step(ORDER, "Laying floor slabs", run))
