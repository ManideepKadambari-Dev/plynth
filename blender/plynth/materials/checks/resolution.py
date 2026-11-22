from .. import CheckEntry, read_size
from ...registry import MATERIAL_CHECKS

ORDER = 30
LOW_PIXELS_PER_METRE = 256


# Warns low_resolution when the base color has fewer pixels per metre than a close look needs.
def run(ctx, found):
    if found.maps["base_color"]["from"] != "file":
        return
    width, height = read_size(found.paths["base_color"])
    across = min(width / found.tile_size["width"], height / found.tile_size["height"])
    if across < LOW_PIXELS_PER_METRE:
        ctx.warn("low_resolution", "The base color has %d pixels per metre; %d or more looks sharp up close." % (
            across, LOW_PIXELS_PER_METRE))


MATERIAL_CHECKS.register("resolution", CheckEntry(ORDER, run))
