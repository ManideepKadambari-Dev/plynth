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


# Removes the cutter object and its mesh.
def remove_cutter(cutter):
    mesh = cutter.data
    bpy.data.objects.remove(cutter, do_unlink=True)
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


# Cuts one opening. The Manifold solver goes first because it keeps merged wall solids closed; the Exact solver
# is the fallback. A cut counts when the walls stay closed and lost at least half the volume the outline should
# remove; otherwise the walls are put back as they were and cut_failed is reported. With partial true the walls
# only hold part of the opening (a kit piece), so any volume up to the opening's own counts.
def cut(ctx, floor, walls, opening, partial=False):
    backup = walls.data.copy()
    before = meshes.volume(walls)
    expected = abs(meshes.polygon_area(opening["outline"])) * opening["thickness"]
    cutter = make_cutter(ctx, opening, walls)
    ok = False
    for solver in SOLVERS:
        try:
            closed = boolean_cut(walls, cutter, solver)
            removed = before - meshes.volume(walls)
            least = 0.0 if partial else expected * MINIMUM_SHARE
            ok = closed and least <= removed <= expected * MAXIMUM_SHARE
        except Exception:
            ok = False
            for modifier in list(walls.modifiers):
                walls.modifiers.remove(modifier)
        if ok:
            break
        restore(walls, backup)
    remove_cutter(cutter)
    bpy.data.meshes.remove(backup)
    if not ok:
        report(ctx, "cut_failed", "error", "The opening could not be cut into its wall.", floor["id"],
               opening["id"])
        return False
    count(ctx, "openings_cut")
    return True


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
    obj = meshes.make_object("SM_%s_Thresholds" % floor["id"], bm, collection, floor["id"], "thresholds")
    meshes.add_slots(obj, names)


BUILD_STEPS.register("openings", Step(ORDER, "Cutting openings", run))
