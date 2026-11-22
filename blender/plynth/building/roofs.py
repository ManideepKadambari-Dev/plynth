import bmesh
from mathutils import Vector

from . import Step, count, report, slot_name
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 50
WELD_DISTANCE = 1e-6
FLAT_LIMIT = 1e-9


# Makes one mesh object for every roof section of every floor, in the floor's collection. A section that cannot
# be built is reported as roof_failed and the others still are.
def run(ctx):
    count(ctx, "roofs", 0)
    for floor in ctx.params["floors"]:
        collection = ctx.state["collections"][floor["id"]]
        for roof in floor["roofs"]:
            try:
                make_roof(ctx, floor, roof, collection)
            except Exception as error:
                report(ctx, "roof_failed", "error",
                       "Roof section %d of %s could not be built: %s" % (roof["index"], floor["id"], error),
                       floor["id"], roof["section"] or floor["id"])


# Returns the unit normal of a ring of 3D points (Newell's method), pointing the way the ring runs anticlockwise.
def ring_normal(points):
    normal = Vector((0.0, 0.0, 0.0))
    for i, p in enumerate(points):
        q = points[(i + 1) % len(points)]
        normal.x += (p[1] - q[1]) * (p[2] + q[2])
        normal.y += (p[2] - q[2]) * (p[0] + q[0])
        normal.z += (p[0] - q[0]) * (p[1] + q[1])
    if normal.length < 1e-12:
        raise ValueError("a face has no area")
    return normal.normalized()


# Returns triangles (index triples over the outline followed by the holes) that fill a planar face, each running
# anticlockwise seen from the side the face looks toward.
def tessellate(outline, holes, normal):
    first = Vector(outline[1]) - Vector(outline[0])
    across = (first - normal * first.dot(normal))
    if across.length < 1e-12:
        raise ValueError("a face has a degenerate edge")
    across.normalize()
    up = normal.cross(across)
    rings = [[(Vector(p).dot(across), Vector(p).dot(up)) for p in ring] for ring in [outline] + list(holes)]
    return meshes.triangulate(rings[0], rings[1:])


# Gives a covering face UVs in metres on its own plane: u along the level line, v up the slope. A flat face
# takes the world X and Y.
def covering_uvs(face, layer, normal):
    level = Vector((-normal.y, normal.x, 0.0))
    if level.length < FLAT_LIMIT:
        for loop in face.loops:
            loop[layer].uv = (loop.vert.co.x, loop.vert.co.y)
        return
    level.normalize()
    slope = normal.cross(level)
    for loop in face.loops:
        loop[layer].uv = (loop.vert.co.dot(level), loop.vert.co.dot(slope))


# Builds the object of one roof section.
def make_roof(ctx, floor, roof, collection):
    if not roof["faces"]:
        raise ValueError("the section has no faces")
    bm = bmesh.new()
    layer = meshes.uv_layer(bm)
    names = []
    made = []
    for source in roof["faces"]:
        outline = source["outline"]
        holes = source["holes"]
        normal = ring_normal(outline)
        points = [p for ring in [outline] + list(holes) for p in ring]
        verts = [bm.verts.new(p) for p in points]
        name = slot_name(ctx, source["slot"])
        if name not in names:
            names.append(name)
        for a, b, c in tessellate(outline, holes, normal):
            face = bm.faces.new([verts[a], verts[b], verts[c]])
            face.material_index = names.index(name)
            made.append((face, source["part"], normal))
    bm.normal_update()
    for face, part, normal in made:
        if part == "covering":
            covering_uvs(face, layer, normal)
        else:
            meshes.project_face(face, layer)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=WELD_DISTANCE)
    name = "SM_%s_Roof_%02d" % (floor["id"], roof["index"])
    obj = meshes.make_object(name, bm, collection, floor["id"], "roof")
    obj["plynth_roof_index"] = roof["index"]
    obj["plynth_roof_section"] = roof["section"] or ""
    meshes.add_slots(obj, names)
    count(ctx, "roofs")


BUILD_STEPS.register("roofs", Step(ORDER, "Building roofs", run))
