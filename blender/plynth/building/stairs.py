import math

import bmesh

from . import Step, count, report, slot_name
from .. import meshes
from ..registry import BUILD_STEPS

ORDER = 40
MIN_AREA = 1e-9


# Builds every stair of every floor: one object per stair, each step a closed prism from its outline.
def run(ctx):
    for floor in ctx.params["floors"]:
        for stair in floor["stairs"]:
            try:
                make_stair(ctx, floor, stair)
            except Exception as error:
                report(ctx, "stair_failed", "error", "Stair %s could not be built: %s" % (stair["id"], error),
                       floor["id"], stair["id"])


# Builds the object of one stair. Steps with a bad outline or no height stop the whole stair.
def make_stair(ctx, floor, stair):
    name = slot_name(ctx, stair["slot"])
    bm = bmesh.new()
    try:
        uv = meshes.uv_layer(bm)
        made = []
        for step in stair["steps"]:
            outline = [(float(p[0]), float(p[1])) for p in step["outline"]]
            if len(outline) < 3 or not all(math.isfinite(c) for p in outline for c in p):
                raise ValueError("a step has a bad outline")
            area = meshes.polygon_area(outline)
            if abs(area) < MIN_AREA:
                raise ValueError("a step has no area")
            if step["top"] <= step["bottom"]:
                raise ValueError("a step has no height")
            if area < 0:
                outline = outline[::-1]
            for face, _, _ in meshes.add_prism(bm, [outline], step["bottom"], step["top"]):
                face.material_index = 0
                made.append(face)
        bm.normal_update()
        for face in made:
            meshes.project_face(face, uv)
        collection = ctx.state["collections"][floor["id"]]
        obj = meshes.make_object("SM_%s_%s_Stair" % (floor["id"], stair["id"]), bm, collection, stair["id"], "stair")
    except Exception:
        bm.free()
        raise
    meshes.add_slots(obj, [name])
    count(ctx, "stairs")


BUILD_STEPS.register("stairs", Step(ORDER, "Building stairs", run))
