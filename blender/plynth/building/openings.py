import bmesh
import bpy
from mathutils import Vector

from . import Step, count, report, slot_name
from .. import meshes, surfaces
from ..registry import BUILD_STEPS

ORDER = 20
CUT_SIDE = 4
MINIMUM_SHARE = 0.5
MAXIMUM_SHARE = 2.0
SOLVERS = ("MANIFOLD", "EXACT")
OVERLAP = 1e-6


# Cuts every opening out of its floor walls, one Boolean cutter at a time, and lays a threshold through every
# doorway that reaches the floor.
def run(ctx):
    for floor in ctx.params["floors"]:
        walls = ctx.state.get("walls_objects", {}).get(floor["id"])
        thresholds = []
        for opening in floor["openings"]:
            if walls is None:
                report(ctx, "cut_failed", "error", "There are no walls to cut the opening into.", floor["id"],
                       opening["id"])
                continue
            if cut(ctx, floor, walls, opening) and opening["threshold"]:
                thresholds.append(opening)
        if thresholds:
            make_thresholds(ctx, floor, thresholds)


# Builds the closed cutter solid of an opening as an object linked into the scene.
def make_cutter(ctx, opening, walls):
    names = list(walls["plynth_walls"])
    if opening["wall"] not in names:
        names.append(opening["wall"])
        walls["plynth_walls"] = names
    wall_index = names.index(opening["wall"])
    right = opening["cutter"]["right"]
    left = opening["cutter"]["left"]
    centre = Vector((opening["centre"][0], opening["centre"][1], 0.0))
    tangent = Vector((opening["tangent"][0], opening["tangent"][1], 0.0))
    flat = [((Vector((p[0], p[1], 0.0)) - centre).dot(tangent), p[2]) for p in right]
    bm = bmesh.new()
    wall_layer = bm.faces.layers.int.new("plynth_wall")
    room_layer = bm.faces.layers.int.new("plynth_room")
    side_layer = bm.faces.layers.int.new("plynth_side")
    right_verts = [bm.verts.new(p) for p in right]
    left_verts = [bm.verts.new(p) for p in left]
    faces = []
    for a, b, c in meshes.triangulate(flat, []):
        faces.append(bm.faces.new([right_verts[a], right_verts[b], right_verts[c]]))
        faces.append(bm.faces.new([left_verts[a], left_verts[c], left_verts[b]]))
    total = len(right_verts)
    for i in range(total):
        j = (i + 1) % total
        faces.append(bm.faces.new([right_verts[i], right_verts[j], left_verts[j], left_verts[i]]))
    bmesh.ops.recalc_face_normals(bm, faces=faces)
    uv = meshes.uv_layer(bm)
    for face in faces:
        meshes.project_face(face, uv)
        face[wall_layer] = wall_index
        face[room_layer] = -1
        face[side_layer] = CUT_SIDE
    mesh = bpy.data.meshes.new("plynth_cutter")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(surfaces.material(slot_name(ctx, opening["slots"]["reveal"])))
    cutter = bpy.data.objects.new("plynth_cutter", mesh)
    bpy.context.scene.collection.objects.link(cutter)
    return cutter


# Removes a temporary object (a cutter or a piece of the walls) and its mesh.
def remove_object(obj):
    mesh = obj.data
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(mesh)


# Runs one Boolean difference of the cutter from the walls with the given solver. Returns true when the walls are
# still a closed solid afterwards.
def boolean_cut(walls, cutter, solver):
    modifier = walls.modifiers.new("plynth_cut", "BOOLEAN")
    modifier.operation = "DIFFERENCE"
    modifier.solver = solver
    modifier.object = cutter
    try:
        modifier.material_mode = "TRANSFER"
    except (AttributeError, TypeError):
        pass
    meshes.apply_modifier(walls, modifier)
    closed = meshes.heal_t_junctions(walls)
    project_cut_faces(walls)
    return closed


# Gives every face a cut made (side 4) UVs projected on the world plane its normal is closest to.
def project_cut_faces(walls):
    bm = bmesh.new()
    bm.from_mesh(walls.data)
    side_layer = bm.faces.layers.int.get("plynth_side")
    uv = meshes.uv_layer(bm)
    bm.normal_update()
    for face in bm.faces:
        if face[side_layer] == CUT_SIDE:
            meshes.project_face(face, uv)
    bm.to_mesh(walls.data)
    bm.free()


# Puts the walls mesh back to the saved copy.
def restore(walls, backup):
    stale = walls.data
    name = stale.name
    walls.data = backup.copy()
    bpy.data.meshes.remove(stale)
    walls.data.name = name


# Cuts one opening. Only the connected solids of the walls the cutter reaches take part, each cut on its own,
# so a low wall that touches a taller one never meets it inside one Boolean (the touching faces break the
# result). The Manifold solver goes first because it keeps merged wall solids closed; the Exact solver is the
# fallback. A cut counts when every solid stays closed and together they lost at least half the volume the
# outline should remove; otherwise the walls are put back as they were and cut_failed is reported. With partial
# true the walls only hold part of the opening (a kit piece), so any volume up to the opening's own counts.
def cut(ctx, floor, walls, opening, partial=False):
    expected = abs(meshes.polygon_area(opening["outline"])) * opening["thickness"]
    cutter = make_cutter(ctx, opening, walls)
    reached, total = reached_pieces(walls, cutter)
    alone = total == 1 and len(reached) == 1
    targets = [walls] if alone else [piece_object(walls, keep) for keep in reached]
    least = 0.0 if partial else expected * MINIMUM_SHARE
    ok = cut_targets(targets, cutter, least, expected * MAXIMUM_SHARE)
    if ok and reached and not alone:
        put_back(walls, set().union(*reached), targets)
    if not alone:
        for target in targets:
            remove_object(target)
    remove_object(cutter)
    if not ok:
        report(ctx, "cut_failed", "error", "The opening could not be cut into its wall.", floor["id"],
               opening["id"])
        return False
    count(ctx, "openings_cut")
    return True


# Cuts the cutter out of every target with one solver, then the next solver when that fails. Returns true when
# every target stayed closed and the volume they lost together lies between least and most; otherwise every
# target is put back as it was.
def cut_targets(targets, cutter, least, most):
    before = sum(meshes.volume(target) for target in targets)
    backups = [target.data.copy() for target in targets]
    ok = False
    for solver in SOLVERS:
        try:
            closed = [boolean_cut(target, cutter, solver) for target in targets]
            removed = before - sum(meshes.volume(target) for target in targets)
            ok = all(closed) and least <= removed <= most
        except Exception:
            ok = False
            for target in targets:
                for modifier in list(target.modifiers):
                    target.modifiers.remove(modifier)
        if ok:
            break
        for target, backup in zip(targets, backups):
            restore(target, backup)
    for backup in backups:
        bpy.data.meshes.remove(backup)
    return ok


# Returns the index of the group a vertex belongs to, following the links of parent to the end.
def root(parent, index):
    while parent[index] != index:
        parent[index] = parent[parent[index]]
        index = parent[index]
    return index


# Returns the connected solids of a mesh, each as the set of its vertex indices.
def mesh_pieces(mesh):
    parent = list(range(len(mesh.vertices)))
    for edge in mesh.edges:
        a = root(parent, edge.vertices[0])
        b = root(parent, edge.vertices[1])
        if a != b:
            parent[a] = b
    groups = {}
    for index in range(len(parent)):
        groups.setdefault(root(parent, index), set()).add(index)
    return list(groups.values())


# Returns the lowest and the highest corner of the box around the points.
def bounds(points):
    low = [min(p[k] for p in points) for k in range(3)]
    high = [max(p[k] for p in points) for k in range(3)]
    return low, high


# Returns true when two boxes share more than a hair of space along every axis.
def boxes_overlap(a, b):
    return all(a[0][k] < b[1][k] - OVERLAP and b[0][k] < a[1][k] - OVERLAP for k in range(3))


# Returns the connected solids of the walls whose box the cutter's box reaches, as vertex index sets, and how
# many solids the walls have in all.
def reached_pieces(walls, cutter):
    box = bounds([cutter.matrix_world @ vertex.co for vertex in cutter.data.vertices])
    matrix = walls.matrix_world
    vertices = walls.data.vertices
    pieces = mesh_pieces(walls.data)
    reached = [keep for keep in pieces if boxes_overlap(bounds([matrix @ vertices[i].co for i in keep]), box)]
    return reached, len(pieces)


# Makes a temporary object, placed like the walls, that holds only the given vertices of the walls and their
# faces, with the same materials and face values.
def piece_object(walls, keep):
    mesh = walls.data.copy()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.index_update()
    bmesh.ops.delete(bm, geom=[vert for vert in bm.verts if vert.index not in keep], context="VERTS")
    bm.to_mesh(mesh)
    bm.free()
    piece = bpy.data.objects.new("plynth_piece", mesh)
    piece.matrix_world = walls.matrix_world.copy()
    bpy.context.scene.collection.objects.link(piece)
    return piece


# Replaces the given vertices of the walls, with their faces, by the meshes of the cut pieces. Each face keeps
# its material: a material the walls do not have yet (the reveal) is added to them.
def put_back(walls, removed, pieces):
    bm = bmesh.new()
    bm.from_mesh(walls.data)
    bm.verts.index_update()
    bmesh.ops.delete(bm, geom=[vert for vert in bm.verts if vert.index in removed], context="VERTS")
    materials = walls.data.materials
    for piece in pieces:
        slots = []
        for material in piece.data.materials:
            found = [i for i, held in enumerate(materials) if held == material]
            if not found:
                materials.append(material)
                found = [len(materials) - 1]
            slots.append(found[0])
        first = len(bm.faces)
        bm.from_mesh(piece.data)
        bm.faces.ensure_lookup_table()
        copy_face_values(bm, bm.faces[first:], piece.data, slots)
    bm.to_mesh(walls.data)
    bm.free()


# Gives the faces just added from a piece mesh their material, face values and UVs from that mesh, matched by
# name. Adding a mesh to a bmesh pairs layers by their order, which may differ between the two meshes.
def copy_face_values(bm, faces, mesh, slots):
    values = [(layer, mesh.attributes[name]) for name, layer in bm.faces.layers.int.items()
              if name in mesh.attributes and mesh.attributes[name].domain == "FACE"
              and mesh.attributes[name].data_type == "INT"]
    uvs = [(layer, mesh.uv_layers[name].data) for name, layer in bm.loops.layers.uv.items()
           if name in mesh.uv_layers]
    for face, polygon in zip(faces, mesh.polygons):
        if polygon.material_index < len(slots):
            face.material_index = slots[polygon.material_index]
        for layer, attribute in values:
            face[layer] = attribute.data[polygon.index].value
        for layer, data in uvs:
            for k, loop in enumerate(face.loops):
                loop[layer].uv = data[polygon.loop_start + k].uv


# Makes the floor level faces that carry the floor through the doorways, one object for the floor.
def make_thresholds(ctx, floor, openings):
    bm = bmesh.new()
    names = []
    for opening in openings:
        name = slot_name(ctx, opening["slots"]["threshold"])
        if name not in names:
            names.append(name)
        faces = meshes.add_flat(bm, opening["threshold"], [], floor["elevation"], True)
        meshes.world_uvs(bm, faces)
        for face in faces:
            face.material_index = names.index(name)
    collection = ctx.state["collections"][floor["id"]]
    obj = meshes.make_object("%s_Thresholds" % floor["id"], bm, collection, floor["id"], "thresholds")
    meshes.add_slots(obj, names)


BUILD_STEPS.register("openings", Step(ORDER, "Cutting openings", run))
