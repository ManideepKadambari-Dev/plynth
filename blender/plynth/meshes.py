import bmesh
import bpy
from mathutils import Vector
from mathutils.geometry import tessellate_polygon

from . import surfaces


# Returns triangles (index triples) that fill a polygon with holes, over the points of the outline followed by
# the points of each hole. Every triangle runs anticlockwise seen from above.
def triangulate(outline, holes):
    rings = [[Vector((p[0], p[1], 0.0)) for p in ring] for ring in [outline] + list(holes)]
    flat = [point for ring in rings for point in ring]
    triangles = []
    for a, b, c in tessellate_polygon(rings):
        cross = (flat[b] - flat[a]).cross(flat[c] - flat[a]).z
        triangles.append((a, b, c) if cross >= 0 else (a, c, b))
    return triangles


# Adds a flat polygon with holes at height z to the mesh, facing up or down, and returns its faces.
def add_flat(bm, outline, holes, z, up=True):
    points = list(outline) + [p for hole in holes for p in hole]
    verts = [bm.verts.new((p[0], p[1], z)) for p in points]
    faces = []
    for a, b, c in triangulate(outline, holes):
        order = (a, b, c) if up else (a, c, b)
        faces.append(bm.faces.new([verts[i] for i in order]))
    return faces


# Adds a solid to the mesh: a bottom cap at z0, a top cap at z1 and one side face per ring edge. rings is the
# outline followed by the holes. Returns a list of (face, kind, edge) where kind is side, top or bottom and
# edge is the index of the ring edge (counted over the outline, then each hole) for side faces.
def add_prism(bm, rings, z0, z1):
    bottom = []
    top = []
    for ring in rings:
        bottom.append([bm.verts.new((p[0], p[1], z0)) for p in ring])
        top.append([bm.verts.new((p[0], p[1], z1)) for p in ring])
    made = []
    flat_bottom = [v for ring in bottom for v in ring]
    flat_top = [v for ring in top for v in ring]
    for a, b, c in triangulate(rings[0], rings[1:]):
        made.append((bm.faces.new([flat_top[a], flat_top[b], flat_top[c]]), "top", None))
        made.append((bm.faces.new([flat_bottom[a], flat_bottom[c], flat_bottom[b]]), "bottom", None))
    edge_index = 0
    for ring_bottom, ring_top in zip(bottom, top):
        total = len(ring_bottom)
        for i in range(total):
            j = (i + 1) % total
            face = bm.faces.new([ring_bottom[i], ring_bottom[j], ring_top[j], ring_top[i]])
            made.append((face, "side", edge_index))
            edge_index += 1
    return made


# Makes an object from a bmesh in the collection, with the plynth custom properties, and frees the bmesh.
def make_object(name, bm, collection, plynth_id, kind):
    bm.normal_update()
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    obj["plynth_id"] = plynth_id
    obj["plynth_kind"] = kind
    collection.objects.link(obj)
    return obj


# Returns the volume of an object mesh in cubic metres, for a closed mesh.
def volume(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    result = bm.calc_volume(signed=True)
    bm.free()
    return result


# Returns true when every edge of the object mesh has exactly two faces.
def is_manifold(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    ok = all(len(edge.link_faces) == 2 for edge in bm.edges)
    bm.free()
    return ok


# Returns the total face area of the object mesh.
def area(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    result = sum(face.calc_area() for face in bm.faces)
    bm.free()
    return result


# Adds the surface materials to the object in the order of names.
def add_slots(obj, names):
    for name in names:
        surfaces.slot_index(obj, name)
