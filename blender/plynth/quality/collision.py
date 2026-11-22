import bmesh
import numpy as np

from . import QualityCheck, finding, result
from ._common import plan_id
from ..registry import QUALITY_CHECKS

ORDER = 100
NEEDS_COLLISION = ("walls", "floor", "item", "kit")
TOLERANCE = 1e-4


# Says whether every edge of the mesh joins exactly two faces.
def is_closed(mesh):
    bm = bmesh.new()
    bm.from_mesh(mesh)
    closed = len(bm.faces) > 0 and all(len(edge.link_faces) == 2 for edge in bm.edges)
    bm.free()
    return closed


# Says whether the mesh is convex: every vertex lies on one side of the plane of every face.
def is_convex(mesh):
    count = len(mesh.polygons)
    if count == 0:
        return False
    points = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
    mesh.vertices.foreach_get("co", points)
    points = points.reshape(-1, 3)
    for polygon in mesh.polygons:
        distances = (points - np.array(polygon.center)) @ np.array(polygon.normal)
        if distances.max() > TOLERANCE and distances.min() < -TOLERANCE:
            return False
    return True


# Fails for a building mesh, item or kit piece without a UCX_ mesh (the door and window fills, ceilings and thresholds need
# none) and for a UCX_ mesh that is not closed or not convex.
def run(ctx):
    findings = []
    covered = {o.get("plynth_collision_for") for o in ctx.state["collision"]}
    for obj in ctx.state["meshes"]:
        if obj.get("plynth_kind") in NEEDS_COLLISION and obj.name not in covered:
            findings.append(finding("%s has no collision mesh." % obj.name, obj.name, plan_id(obj)))
    for obj in ctx.state["collision"]:
        if not is_closed(obj.data):
            findings.append(finding("%s is not closed." % obj.name, obj.name, plan_id(obj)))
        elif not is_convex(obj.data):
            findings.append(finding("%s is not convex. Split it into convex pieces." % obj.name, obj.name,
                                    plan_id(obj)))
    if findings:
        return result("fail", "Some collision is missing or not usable.", findings)
    return result("pass", "Every walls, floor and item mesh has closed, convex collision.")


QUALITY_CHECKS.register("collision", QualityCheck(ORDER, "scene", "Collision", run))
