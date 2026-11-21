from ..registry import CHECKS


# Warns when the model has more triangles than the limit in the job.
def run(ctx):
    limit = int(ctx.params["triangle_warning"])
    triangles = ctx.state["triangles"]
    if triangles > limit:
        ctx.warn("high_polycount", "The model has %d triangles, more than the limit of %d." % (triangles, limit))


CHECKS.register("polycount", run)
