import math

import bmesh
import bpy
from mathutils import Vector

from . import Step, report, slot_name
from . import bevel as bevel_step
from .. import meshes, surfaces, sweep_mesh
from ..registry import BUILD_STEPS

ORDER = 55
SITE_COLLECTION = "Site"
MIN_AREA = 1e-9
MIN_HEIGHT = 1e-6
MIN_LENGTH = 1e-6
SAME_POINT = 1e-9
# A face whose normal is within 45 degrees of straight up or down counts as flat for the along UVs.
FLAT_LIMIT = math.sqrt(0.5)

# The neutral looks of the surfaces parts use (base color, roughness, metallic, alpha), added to the build's
# neutral looks when the step is loaded, so a slot without a material shows its own look instead of a wall's.
PART_LOOKS = {
    "Light_Strip": ((0.94, 0.94, 0.92), 0.35, 0.0, 1.0),
    "Deck": ((0.56, 0.42, 0.30), 0.75, 0.0, 1.0),
    "Foundation": ((0.55, 0.55, 0.54), 0.9, 0.0, 1.0),
    "Ground": ((0.30, 0.42, 0.22), 0.95, 0.0, 1.0),
    "Paving": ((0.62, 0.61, 0.58), 0.85, 0.0, 1.0),
    "Fence": ((0.50, 0.40, 0.30), 0.8, 0.0, 1.0),
    "Boundary_Wall": ((0.66, 0.64, 0.60), 0.9, 0.0, 1.0),
}
for _surface, _look in PART_LOOKS.items():
    surfaces.LOOKS.setdefault(_surface, _look)


# Builds every part of every floor in its floor's collection, then the site parts in the Site collection: one
# object per part from its prisms, faces and sweeps, each face with the material of its slot. A part that
# cannot be built gives part_failed and the build goes on. The made objects are kept by their job name, so the
# collision step can give them their collision.
def run(ctx):
    made = ctx.state.setdefault("part_objects", {})
    for floor in ctx.params["floors"]:
        parts = floor.get("parts", [])
        if parts:
            collection = ctx.state["collections"][floor["id"]]
            for part in parts:
                build_part(ctx, part, collection, floor["id"], made)
    site = ctx.params.get("site_parts", [])
    if site:
        collection = site_collection(ctx)
        for part in site:
            build_part(ctx, part, collection, None, made)


# Returns the Site collection, made and linked to the scene on first use.
def site_collection(ctx):
    found = ctx.state.get("site_collection")
    if found is None:
        found = bpy.data.collections.new(SITE_COLLECTION)
        bpy.context.scene.collection.children.link(found)
        ctx.state["site_collection"] = found
    return found


# Builds one part and records it, or reports why it could not be built.
def build_part(ctx, part, collection, floor_id, made):
    try:
        obj = make_part(ctx, part, collection)
    except Exception as error:
        report(ctx, "part_failed", "warning", "The %s %s could not be built: %s" % (
            part.get("plynth_kind", "part"), part.get("object", ""), error), floor_id, part.get("plynth_id"))
        return
    made[part["object"]] = obj
    round_edges(ctx, obj, part, floor_id)
    meshes.make_viewer_safe(obj.data)


# Returns the slot names a part uses, in the order they first appear.
def part_slots(part):
    names = []
    for prism in part["prisms"]:
        names += [prism["slot"], prism.get("side_slot") or prism["slot"]]
    names += [face["slot"] for face in part["faces"]]
    names += [sweep["slot"] for sweep in part["sweeps"]]
    unique = []
    for name in names:
        if name not in unique:
            unique.append(name)
    return unique


# Makes the object of one part with its UVs in metres and its material slots.
def make_part(ctx, part, collection):
    names = part_slots(part)
    index = {name: i for i, name in enumerate(names)}
    bm = bmesh.new()
    try:
        uv = meshes.uv_layer(bm)
        plain = []
        swept = []
        for prism in part["prisms"]:
            plain += add_prism(bm, prism, index)
        for face in part["faces"]:
            plain += add_face(bm, face, index[face["slot"]])
        for sweep in part["sweeps"]:
            swept += add_sweep(bm, sweep, index[sweep["slot"]], uv)
        if not plain and not swept:
            raise ValueError("it has no faces")
        bm.normal_update()
        if part["uv"] == "along":
            direction = along_direction(part)
            for face in plain:
                along_uv(face, uv, direction)
        else:
            for face in plain + swept:
                meshes.project_face(face, uv)
        obj = meshes.make_object(part["object"], bm, collection, part["plynth_id"], part["plynth_kind"])
    except Exception:
        if bm.is_valid:
            bm.free()
        raise
    meshes.add_slots(obj, [slot_name(ctx, name) for name in names])
    return obj


# Returns a ring of points as tuples, without a repeated closing point or points that repeat the one before.
# Raises ValueError for a ring with fewer than three points or a number that is not finite.
def clean_ring(points, size):
    ring = []
    for point in points:
        values = tuple(float(c) for c in point[:size])
        if len(values) != size or not all(math.isfinite(c) for c in values):
            raise ValueError("a point is not %d finite numbers" % size)
        if ring and all(abs(a - b) <= SAME_POINT for a, b in zip(ring[-1], values)):
            continue
        ring.append(values)
    if len(ring) > 1 and all(abs(a - b) <= SAME_POINT for a, b in zip(ring[0], ring[-1])):
        ring.pop()
    if len(ring) < 3:
        raise ValueError("an outline has fewer than three points")
    return ring


# Adds one prism: its top and bottom with the slot and its sides with the side slot. Returns its faces.
def add_prism(bm, prism, index):
    outline = clean_ring(prism["outline"], 2)
    holes = [clean_ring(hole, 2) for hole in prism["holes"]]
    bottom, top = float(prism["bottom"]), float(prism["top"])
    if not (math.isfinite(bottom) and math.isfinite(top)) or top - bottom < MIN_HEIGHT:
        raise ValueError("a prism has no height")
    area = meshes.polygon_area(outline)
    if abs(area) < MIN_AREA:
        raise ValueError("a prism has no area")
    if area < 0:
        outline = outline[::-1]
    holes = [hole[::-1] if meshes.polygon_area(hole) > 0 else hole for hole in holes]
    cap = index[prism["slot"]]
    side = index[prism.get("side_slot") or prism["slot"]]
    faces = []
    for face, kind, _ in meshes.add_prism(bm, [outline] + holes, bottom, top):
        face.material_index = side if kind == "side" else cap
        faces.append(face)
    return faces


# Returns the unit normal of a ring of 3D points by Newell's method, or None when it has no area.
def ring_normal(ring):
    normal = Vector((0.0, 0.0, 0.0))
    for i, p in enumerate(ring):
        q = ring[(i + 1) % len(ring)]
        normal.x += (p[1] - q[1]) * (p[2] + q[2])
        normal.y += (p[2] - q[2]) * (p[0] + q[0])
        normal.z += (p[0] - q[0]) * (p[1] + q[1])
    if normal.length < MIN_AREA:
        return None
    return normal.normalized()


# Adds one planar face with holes, split into triangles that face the way its outline runs anticlockwise.
# Returns its faces.
def add_face(bm, face, slot):
    outline = clean_ring(face["outline"], 3)
    holes = [clean_ring(hole, 3) for hole in face["holes"]]
    normal = ring_normal(outline)
    if normal is None:
        raise ValueError("a face has no area")
    u_axis = normal.orthogonal().normalized()
    v_axis = normal.cross(u_axis)
    flat = [[(Vector(p).dot(u_axis), Vector(p).dot(v_axis)) for p in ring] for ring in [outline] + holes]
    points = [p for ring in [outline] + holes for p in ring]
    verts = [bm.verts.new(p) for p in points]
    faces = []
    for a, b, c in meshes.triangulate(flat[0], flat[1:]):
        made = bm.faces.new((verts[a], verts[b], verts[c]))
        made.material_index = slot
        faces.append(made)
    return faces


# Adds one sweep with the UVs the sweep gives (u along the path, v round the profile, in metres). Returns its
# faces.
def add_sweep(bm, sweep, slot, uv):
    mesh = sweep_mesh.sweep_mesh(sweep["sections"], bool(sweep["closed"]))
    verts = [bm.verts.new(p) for p in mesh["vertices"]]
    faces = []
    for face, corners in zip(mesh["faces"], mesh["uvs"]):
        made = bm.faces.new([verts[i] for i in face])
        made.material_index = slot
        for loop, (u, v) in zip(made.loops, corners):
            loop[uv].uv = (u, v)
        faces.append(made)
    return faces


# Returns the level unit direction the along UVs run in: that of the longest prism side, else of the longest
# sweep from its first to its last section, else of the longest face edge, else world X.
def along_direction(part):
    best = (0.0, None)
    for prism in part["prisms"]:
        ring = prism["outline"]
        for i, p in enumerate(ring):
            q = ring[(i + 1) % len(ring)]
            best = longer(best, Vector((q[0] - p[0], q[1] - p[1], 0.0)))
    if best[1] is None:
        for sweep in part["sweeps"]:
            first = centre(sweep["sections"][0])
            last = centre(sweep["sections"][-1])
            best = longer(best, Vector((last.x - first.x, last.y - first.y, 0.0)))
    if best[1] is None:
        for face in part["faces"]:
            ring = face["outline"]
            for i, p in enumerate(ring):
                q = ring[(i + 1) % len(ring)]
                best = longer(best, Vector((q[0] - p[0], q[1] - p[1], 0.0)))
    return best[1].normalized() if best[1] is not None else Vector((1.0, 0.0, 0.0))


# Returns whichever of the best (length, vector) so far and a new vector is longer.
def longer(best, vector):
    length = vector.length
    if length > MIN_LENGTH and length > best[0]:
        return (length, vector)
    return best


# Returns the middle of a ring of 3D points.
def centre(ring):
    total = Vector((0.0, 0.0, 0.0))
    for p in ring:
        total += Vector(p[:3])
    return total / len(ring)


# Gives one face UVs in metres for the along mode: flat faces get u along the direction and v across it; the
# others get u along the face's level edge, turned to follow the direction, and v up the face.
def along_uv(face, layer, direction):
    normal = face.normal
    across = Vector((-direction.y, direction.x, 0.0))
    if abs(normal.z) >= FLAT_LIMIT:
        for loop in face.loops:
            co = loop.vert.co
            loop[layer].uv = (co.dot(direction), co.dot(across))
        return
    tangent = Vector((-normal.y, normal.x, 0.0))
    tangent = tangent.normalized() if tangent.length > MIN_LENGTH else direction.copy()
    if abs(tangent.dot(direction)) >= abs(tangent.dot(across)):
        tangent = tangent if tangent.dot(direction) >= 0.0 else -tangent
    else:
        tangent = tangent if tangent.dot(across) >= 0.0 else -tangent
    up = Vector((0.0, 0.0, 1.0)) - normal * normal.z
    up = up.normalized() if up.length > MIN_LENGTH else Vector((0.0, 0.0, 1.0))
    for loop in face.loops:
        co = loop.vert.co
        loop[layer].uv = (co.dot(tangent), co.dot(up))


# Rounds the sharp edges of a part with the build's bevel when the part asks for it and holds a solid. The
# result is kept only when it is sound, as for the walls.
def round_edges(ctx, obj, part, floor_id):
    settings = ctx.params["settings"]
    width = settings.get("bevel_width", 0.0)
    if not part["bevel"] or width <= 0 or not (part["prisms"] or part["sweeps"]):
        return
    if meshes.volume(obj) <= MIN_AREA:
        return
    bevel_step.bevel(ctx, floor_id, obj, width, settings.get("bevel_segments", 1),
                     message="The edges of %s could not be rounded, so they stay sharp." % obj.name,
                     plan_id=part["plynth_id"])


BUILD_STEPS.register("parts", Step(ORDER, "Building parts", run))
