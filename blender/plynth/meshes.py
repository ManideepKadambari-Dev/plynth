import bmesh
import bpy
from mathutils import Vector, kdtree
KDTree = kdtree.KDTree
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


# Returns the area of a polygon given as (u, v) points, positive when anticlockwise.
def polygon_area(points):
    total = 0.0
    for i, (x0, y0) in enumerate(points):
        x1, y1 = points[(i + 1) % len(points)]
        total += x0 * y1 - x1 * y0
    return total / 2.0


# Replaces the object mesh by what its modifiers make of it and removes the modifier. The new mesh keeps the
# name of the old one.
def apply_modifier(obj, modifier):
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    new_mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))
    old = obj.data
    name = old.name
    obj.modifiers.remove(modifier)
    obj.data = new_mesh
    if old.users == 0:
        bpy.data.meshes.remove(old)
    new_mesh.name = name


# Adds a prism whose cross section is a polygon with holes in the (u, v) plane, extruded between w0 and w1.
# place(u, v, w) gives the world point. Every face gets the material index slot. Returns the new faces.
def add_prism_uv(bm, outline, holes, w0, w1, place, slot):
    rings = [outline] + list(holes)
    back = [[bm.verts.new(place(u, v, w0)) for u, v in ring] for ring in rings]
    front = [[bm.verts.new(place(u, v, w1)) for u, v in ring] for ring in rings]
    flat_back = [vert for ring in back for vert in ring]
    flat_front = [vert for ring in front for vert in ring]
    faces = []
    for a, b, c in triangulate(outline, holes):
        faces.append(bm.faces.new([flat_front[a], flat_front[b], flat_front[c]]))
        faces.append(bm.faces.new([flat_back[a], flat_back[c], flat_back[b]]))
    for ring_back, ring_front in zip(back, front):
        total = len(ring_back)
        for i in range(total):
            j = (i + 1) % total
            faces.append(bm.faces.new([ring_back[i], ring_back[j], ring_front[j], ring_front[i]]))
    for face in faces:
        face.material_index = slot
    bmesh.ops.recalc_face_normals(bm, faces=faces)
    return faces


# Closes the cracks an Exact Boolean can leave where a merged face skips a vertex that its neighbour still has.
# Each boundary edge with a vertex lying on it is split there and the new vertex is welded to the old one.
# Returns true when the mesh is closed afterwards.
def heal_t_junctions(obj, tolerance=1e-5):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    for _ in range(500):
        bad = [edge for edge in bm.edges if len(edge.link_faces) == 1]
        if not bad:
            break
        bm.verts.ensure_lookup_table()
        tree = KDTree(len(bm.verts))
        for vert in bm.verts:
            tree.insert(vert.co, vert.index)
        tree.balance()
        split = False
        for edge in bad:
            a, b = edge.verts[0], edge.verts[1]
            direction = b.co - a.co
            length = direction.length
            if length < 1e-9:
                continue
            found = []
            for _, index, _ in tree.find_range((a.co + b.co) / 2.0, length / 2.0 + tolerance):
                vert = bm.verts[index]
                if vert is a or vert is b:
                    continue
                t = (vert.co - a.co).dot(direction) / (length * length)
                if t <= 1e-6 or t >= 1.0 - 1e-6:
                    continue
                if (a.co + direction * t - vert.co).length <= tolerance:
                    found.append((t, vert))
            if not found:
                continue
            t, vert = min(found, key=lambda pair: pair[0])
            _, fresh = bmesh.utils.edge_split(edge, a, t)
            bmesh.ops.weld_verts(bm, targetmap={fresh: vert})
            split = True
            break
        if not split:
            break
    closed = all(len(edge.link_faces) == 2 for edge in bm.edges)
    bm.to_mesh(obj.data)
    bm.free()
    return closed
