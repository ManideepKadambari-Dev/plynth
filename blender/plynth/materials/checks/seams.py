import numpy as np

from .. import CheckEntry, read_pixels
from ...registry import MATERIAL_CHECKS

ORDER = 20
MIN_JUMP = 0.08
JUMP_RATIO = 3.0


# Returns the mean difference of the first and last lines of an array (lines along the first axis), and the
# mean difference between neighbouring lines inside it.
def edge_jump(lines):
    seam = float(np.abs(lines[0] - lines[-1]).mean())
    inner = float(np.abs(np.diff(lines, axis=0)).mean())
    return seam, inner


# Says whether the opposite edges of the image differ much more than neighbouring lines do.
def has_seam(lines):
    seam, inner = edge_jump(lines)
    return seam > MIN_JUMP and seam > JUMP_RATIO * inner + 0.03


# Warns visible_seams when the left and right, or top and bottom, edges of the base color differ strongly,
# which means the image will show lines where it repeats.
def run(ctx, found):
    if found.maps["base_color"]["from"] not in ("file", "baked"):
        return
    rgb = read_pixels(found.paths["base_color"])[:, :, :3]
    sides = []
    if has_seam(np.swapaxes(rgb, 0, 1)):
        sides.append("left and right")
    if has_seam(rgb):
        sides.append("top and bottom")
    if sides:
        ctx.warn("visible_seams", "The %s edges of the base color differ, so it may show seams where it repeats." %
                 " and ".join(sides))


MATERIAL_CHECKS.register("seams", CheckEntry(ORDER, run))
