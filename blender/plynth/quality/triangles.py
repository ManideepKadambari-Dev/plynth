from . import QualityCheck, finding, result
from ._common import exported_objects, plan_id
from ..registry import QUALITY_CHECKS

ORDER = 80


# Counts the triangles of one mesh object.
def triangles_of(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


# Fails for a mesh with no triangles. Warns above object_triangles for one mesh or total_triangles in all. The
# kit pieces are checked like the rest but are not part of the building's total.
def run(ctx):
    limits = ctx.params["limits"]
    failures = []
    warnings = []
    total = 0
    for obj in exported_objects(ctx):
        count = triangles_of(obj)
        if count == 0:
            failures.append(finding("%s has no triangles." % obj.name, obj.name, plan_id(obj)))
            continue
        if obj in ctx.state["meshes"] and obj.get("plynth_kind") != "kit":
            total += count
        if count > limits["object_triangles"]:
            warnings.append(finding("%s has %d triangles, more than %d." % (obj.name, count,
                                                                            limits["object_triangles"]),
                                    obj.name, plan_id(obj)))
    if total > limits["total_triangles"]:
        warnings.append(finding("The building has %d triangles, more than %d." % (total, limits["total_triangles"])))
    if failures:
        return result("fail", "Some meshes are empty.", failures + warnings)
    if warnings:
        return result("warning", "Some meshes have many triangles.", warnings)
    return result("pass", "The building has %d triangles and no mesh is empty." % total)


QUALITY_CHECKS.register("triangles", QualityCheck(ORDER, "scene", "Triangle counts", run))
