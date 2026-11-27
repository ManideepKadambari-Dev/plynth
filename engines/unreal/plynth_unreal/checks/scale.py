from .. import ue
from ..registry import check

TOLERANCE_CM = 0.5
# How far the walls group may reach past the walls (sills, frames) and the whole building past them (roof
# overhangs, steps), in centimetres. A wrong unit is a factor of 100, far outside both.
STRUCTURE_SLACK_CM = 50.0
BUILDING_SLACK_CM = 300.0
STRUCTURE_GROUP = "structure"


# Checks the imported meshes have the size and place the walls' expected bounds give, in centimetres.
@check("scale", 10)
def run(ctx):
    bounds = ctx.job["checks"]["expected_bounds"]
    if bounds is None:
        return "warning", "The job gave no expected bounds, so the scale was not checked."
    boxes = {}
    for mesh in ctx.job["meshes"]:
        asset = ue.load(ctx.mesh_path(mesh["asset"]))
        if asset is None:
            return "fail", "The mesh %s is missing." % mesh["asset"]
        box = asset.get_bounding_box()
        boxes[mesh["group"]] = (vector(box.min), vector(box.max))
    if ctx.job["grouped"] and STRUCTURE_GROUP in boxes:
        box, slack, what = boxes[STRUCTURE_GROUP], STRUCTURE_SLACK_CM, "The walls group"
    else:
        box, slack, what = union(list(boxes.values())), BUILDING_SLACK_CM, "The building"
    mirrored = to_unreal(bounds, True)
    if fits(box, mirrored, slack):
        return "pass", "%s spans %s cm, which holds the walls' %s cm." % (what, show(box), show(mirrored))
    straight = to_unreal(bounds, False)
    if fits(box, straight, slack):
        return "warning", ("%s has the right size but its Y axis is not mirrored the way Unreal's FBX import "
                           "mirrors it: %s cm against %s cm." % (what, show(box), show(mirrored)))
    return "fail", "%s spans %s cm, but the walls need %s cm." % (what, show(box), show(mirrored))


# Returns the three numbers of a vector.
def vector(value):
    return [float(value.x), float(value.y), float(value.z)]


# Returns the box around several boxes.
def union(boxes):
    low = [min(box[0][axis] for box in boxes) for axis in range(3)]
    high = [max(box[1][axis] for box in boxes) for axis in range(3)]
    return low, high


# Turns bounds in metres in Plynth's frame into centimetres in Unreal's, where the FBX import mirrors Y.
def to_unreal(bounds, mirror_y):
    low = [value * 100.0 for value in bounds["min"]]
    high = [value * 100.0 for value in bounds["max"]]
    if mirror_y:
        low[1], high[1] = -high[1], -low[1]
    return low, high


# Returns True when the box holds the expected box and reaches past it by no more than the slack.
def fits(box, expected, slack):
    for axis in range(3):
        if box[0][axis] > expected[0][axis] + TOLERANCE_CM or box[1][axis] < expected[1][axis] - TOLERANCE_CM:
            return False
        if box[0][axis] < expected[0][axis] - slack or box[1][axis] > expected[1][axis] + slack:
            return False
    return True


# Returns a box as readable text.
def show(box):
    return "from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)" % tuple(box[0] + box[1])
