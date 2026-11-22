import bpy

from . import QualityCheck, finding, result
from ._common import exported_objects, plan_id
from ..registry import QUALITY_CHECKS

ORDER = 40
TOLERANCE = 1e-6


# Says whether a matrix is the identity.
def is_identity(matrix):
    return all(abs(matrix[r][c] - (1.0 if r == c else 0.0)) <= TOLERANCE for r in range(4) for c in range(4))


# Fails for an exported object whose transform is not the identity or that has a parent.
def run(ctx):
    bpy.context.view_layer.update()
    findings = []
    for obj in exported_objects(ctx):
        if obj.parent is not None:
            findings.append(finding("%s has a parent (%s)." % (obj.name, obj.parent.name), obj.name, plan_id(obj)))
        elif not is_identity(obj.matrix_world):
            findings.append(finding("%s is moved, turned or scaled. Apply its transform." % obj.name, obj.name,
                                    plan_id(obj)))
    if findings:
        return result("fail", "Some objects do not sit at the origin with applied transforms.", findings)
    return result("pass", "Every object has an identity transform and no parent.")


QUALITY_CHECKS.register("transforms", QualityCheck(ORDER, "scene", "Pivots and applied transforms", run))
