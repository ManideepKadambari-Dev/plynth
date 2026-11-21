import math

import bpy

from . import Step, report
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 24
ANGLE = math.radians(30.0)
VOLUME_TOLERANCE = 0.02


# Rounds the sharp edges of every walls object with a Bevel modifier. Skipped when the width is 0. If the
# result is damaged the walls stay sharp and bevel_failed is reported.
def run(ctx):
    width = ctx.params["settings"]["bevel_width"]
    if width <= 0:
        return
    segments = ctx.params["settings"]["bevel_segments"]
    for floor_id, walls in ctx.state.get("walls_objects", {}).items():
        bevel(ctx, floor_id, walls, width, segments)


# Bevels one walls object and keeps the result only when it is still closed and the volume is about the same.
def bevel(ctx, floor_id, walls, width, segments):
    backup = walls.data.copy()
    before = meshes.volume(walls)
    was_closed = meshes.is_manifold(walls)
    try:
        modifier = walls.modifiers.new("plynth_bevel", "BEVEL")
        modifier.width = width
        modifier.segments = segments
        modifier.limit_method = "ANGLE"
        modifier.angle_limit = ANGLE
        meshes.apply_modifier(walls, modifier)
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
        report(ctx, "bevel_failed", "warning", "The wall edges could not be rounded, so they stay sharp.", floor_id,
               floor_id)
        return
    bpy.data.meshes.remove(backup)


BUILD_STEPS.register("bevel", Step(ORDER, "Rounding edges", run))
