from .. import CheckEntry, file_is_magenta
from ...registry import MATERIAL_CHECKS

ORDER = 5


# Warns missing_texture for every map that is all magenta, the colour Blender gives an image it could not load.
def run(ctx, found):
    for channel, path in sorted(found.paths.items()):
        if file_is_magenta(path):
            ctx.warn("missing_texture", "The %s map is all magenta, the colour Blender gives an image it could not "
                                        "load." % channel.replace("_", " "))


MATERIAL_CHECKS.register("magenta", CheckEntry(ORDER, run))
