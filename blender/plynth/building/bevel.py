import math

import bmesh
import bpy

from . import Step, report
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 24
ANGLE = math.radians(30.0)
VOLUME_TOLERANCE = 0.02
TOP_SIDE = 3
WEIGHT = "bevel_weight_edge"


# Rounds the sharp edges of every walls object with a Bevel modifier. Skipped when the width is 0. If the
# result is damaged the walls stay sharp and bevel_failed is reported.
def run(ctx):
    width = ctx.params["settings"]["bevel_width"]
    if width <= 0:
        return
    segments = ctx.params["settings"]["bevel_segments"]
    for floor_id, walls in ctx.state.get("walls_objects", {}).items():
        bevel(ctx, floor_id, walls, width, segments)


# Marks the sharp edges of the walls for the bevel, leaving out the edges of the top and bottom faces: a slab,
# a floor or the ground covers those, and a rounded edge there shows as a groove along the outside wall.
def mark_edges(walls):
    bm = bmesh.new()
    bm.from_mesh(walls.data)
    sides = bm.faces.layers.int.get("plynth_side")
    weights = bm.edges.layers.float.get(WEIGHT) or bm.edges.layers.float.new(WEIGHT)
    for edge in bm.edges:
        faces = edge.link_faces
        if len(faces) != 2 or edge.calc_face_angle(0.0) < ANGLE:
            continue
        if sides is not None and any(face[sides] == TOP_SIDE for face in faces):
            continue
        edge[weights] = 1.0
    bm.to_mesh(walls.data)
    bm.free()


# Bevels one walls object and keeps the result only when it is still closed and the volume is about the same.
# With weighted true only the edges that carry a bevel weight are rounded (the kit pieces mark theirs). Other
# objects, such as build parts, pass their own sentence and plan id for the problem.
def bevel(ctx, floor_id, walls, width, segments, weighted=False, message=None, plan_id=None):
    backup = walls.data.copy()
    before = meshes.volume(walls)
    was_closed = meshes.is_manifold(walls)
    try:
        if not weighted:
            mark_edges(walls)
        modifier = walls.modifiers.new("plynth_bevel", "BEVEL")
        modifier.width = width
        modifier.segments = segments
        modifier.limit_method = "WEIGHT"
        meshes.apply_modifier(walls, modifier)
        if not weighted and WEIGHT in walls.data.attributes:
            walls.data.attributes.remove(walls.data.attributes[WEIGHT])
        after = meshes.volume(walls)
        ok = abs(after - before) <= before * VOLUME_TOLERANCE and (meshes.is_manifold(walls) or not was_closed)
    except Exception:
        ok = False
    if not ok:
        stale = walls.data
        name = stale.name
        walls.data = backup
        bpy.data.meshes.remove(stale)
        backup.name = name
        report(ctx, "bevel_failed", "warning", message or "The wall edges could not be rounded, so they stay sharp.",
               floor_id, plan_id or floor_id)
        return
    bpy.data.meshes.remove(backup)
    meshes.reproject_flat_uvs(walls)


BUILD_STEPS.register("bevel", Step(ORDER, "Rounding edges", run))
