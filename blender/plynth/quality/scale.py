import bpy

from . import QualityCheck, finding, result
from ._common import plan_id
from .. import scene
from ..registry import QUALITY_CHECKS

ORDER = 30
AXES = "xyz"


# Returns the objects of the building walls.
def wall_objects(ctx):
    return [o for o in ctx.state["meshes"] if o.get("plynth_kind") == "walls"]


# Compares bounds given as (low, high) with the expected ones. Returns a sentence for each side that is off.
def bound_differences(bounds, expected, tolerance):
    problems = []
    for side, name in ((0, "lowest"), (1, "highest")):
        want = expected["min" if side == 0 else "max"]
        for axis in range(3):
            got = bounds[side][axis]
            if abs(got - want[axis]) > tolerance:
                problems.append("The %s %s of the walls is %.4f m, the plan says %.4f m." % (
                    name, AXES[axis], got, want[axis]))
    return problems


# Fails unless the scene is metric with unit scale 1 and the walls have the bounds the plan gives.
def run(ctx):
    findings = []
    units = bpy.context.scene.unit_settings
    if units.system != "METRIC":
        findings.append(finding("The scene units are %s, not metric." % units.system.lower()))
    if abs(units.scale_length - 1.0) > 1e-9:
        findings.append(finding("The unit scale is %g, it must be 1." % units.scale_length))
    expected = ctx.params.get("expected_bounds")
    if expected is not None:
        bpy.context.view_layer.update()
        walls = wall_objects(ctx)
        bounds = scene.world_bounds(walls) if walls else None
        if bounds is None:
            findings.append(finding("No wall mesh was found to measure."))
        else:
            tolerance = ctx.params["limits"]["bounds_tolerance"]
            findings += [finding(text, walls[0].name, plan_id(walls[0]))
                         for text in bound_differences(bounds, expected, tolerance)]
    if findings:
        return result("fail", "The scale or units are wrong.", findings)
    return result("pass", "The scene is metric at scale 1 and the walls match the plan.")


QUALITY_CHECKS.register("scale", QualityCheck(ORDER, "scene", "Scale and units", run))
