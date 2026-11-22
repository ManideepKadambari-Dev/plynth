from .. import CheckEntry, read_size
from ...registry import MATERIAL_CHECKS

ORDER = 10


# Warns map_size_mismatch when a map made from an image has another pixel size than the base color.
def run(ctx, found):
    base = read_size(found.paths["base_color"])
    for channel, path in found.paths.items():
        if channel == "base_color" or found.maps[channel]["from"] != "file":
            continue
        size = read_size(path)
        if size != base:
            ctx.warn("map_size_mismatch", "%s is %d x %d pixels but base color is %d x %d." % (
                found.maps[channel]["detail"], size[0], size[1], base[0], base[1]))


MATERIAL_CHECKS.register("map_sizes", CheckEntry(ORDER, run))
