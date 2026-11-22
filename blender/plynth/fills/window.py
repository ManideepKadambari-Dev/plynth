import bmesh

from . import FillEntry
from . import common
from .. import meshes
from ..registry import OPENING_FILLS

SURFACES = ["Frame", "Glass"]
FRAME_DEPTH = 0.07
GLASS_THICKNESS = 0.006


# Makes one window: a frame band seven centimetres deep and a thin glass pane inside the inner outline.
# Returns the list of made objects.
def build(ctx, floor, opening):
    place = common.placer(floor, opening)
    bm = bmesh.new()
    ring, holes = common.frame_band(opening["outline"], opening["inner_outline"])
    meshes.add_prism_uv(bm, ring, holes, -FRAME_DEPTH / 2.0, FRAME_DEPTH / 2.0, place, 0)
    meshes.add_prism_uv(bm, opening["inner_outline"], [], -GLASS_THICKNESS / 2.0, GLASS_THICKNESS / 2.0, place, 1)
    return [common.finish(ctx, floor, opening, bm, "Window", "window", SURFACES)]


OPENING_FILLS.register("window", FillEntry(("window",), build))
