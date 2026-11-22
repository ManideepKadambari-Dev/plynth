import bmesh

from . import FillEntry
from . import common
from .. import meshes
from ..registry import OPENING_FILLS

PARTS = [("Frame", "frame"), ("Door", "door"), ("Handle", "handle")]
FRAME_PROUD = 0.005
LEAF_THICKNESS = 0.04
HANDLE_LENGTH = 0.12
HANDLE_THICKNESS = 0.03
HANDLE_HEIGHT = 0.03
HANDLE_INSET = 0.06
HANDLE_V = 1.0
HANDLE_BELOW_TOP = 0.1


# Makes one door: a frame band, a closed leaf inside the inner outline and a handle on both faces on the side
# away from the hinge. Returns the list of made objects.
def build(ctx, floor, opening):
    place = common.placer(floor, opening)
    names, slot = common.part_slots(ctx, opening, PARTS)
    half = opening["thickness"] / 2.0 + FRAME_PROUD
    inner = opening["inner_outline"]
    bm = bmesh.new()
    ring, holes = common.frame_band(opening["outline"], inner)
    meshes.add_prism_uv(bm, ring, holes, -half, half, place, slot[0])
    leaf = LEAF_THICKNESS / 2.0
    meshes.add_prism_uv(bm, inner, [], -leaf, leaf, place, slot[1])
    top = max(v for _, v in inner)
    height = min(HANDLE_V, top - HANDLE_BELOW_TOP)
    span = common.span_at(inner, height)
    if span is not None:
        if opening["hinge"] == "start":
            far = span[1] - HANDLE_INSET
            u0, u1 = far - HANDLE_LENGTH, far
        else:
            far = span[0] + HANDLE_INSET
            u0, u1 = far, far + HANDLE_LENGTH
        v0, v1 = height - HANDLE_HEIGHT / 2.0, height + HANDLE_HEIGHT / 2.0
        common.add_box(bm, place, u0, v0, leaf, u1, v1, leaf + HANDLE_THICKNESS, slot[2])
        common.add_box(bm, place, u0, v0, -leaf - HANDLE_THICKNESS, u1, v1, -leaf, slot[2])
    return [common.finish(ctx, floor, opening, bm, "Door", "door", names)]


OPENING_FILLS.register("door", FillEntry(("door",), build))
