from ..registry import CHECKS


# Warns once for each texture file the model names that was not found.
def run(ctx):
    for name in ctx.state["missing_textures"]:
        ctx.warn("missing_texture", "The texture file %s was not found." % name)


CHECKS.register("textures", run)
