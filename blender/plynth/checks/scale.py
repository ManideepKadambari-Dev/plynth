from ..registry import CHECKS

LARGEST_SIDE_MAX = 30.0
LARGEST_SIDE_MIN = 0.02


# Warns when the largest side is far from what a piece of furniture or a prop would measure.
def run(ctx):
    size = ctx.state["size"]
    largest = max(size["width"], size["depth"], size["height"])
    if largest > LARGEST_SIDE_MAX:
        ctx.warn("scale_suspicious", "The largest side is %.2f m, which is very large. "
                 "The file may use other units. Import with a scale (for example --scale 0.01)." % largest)
    elif largest < LARGEST_SIDE_MIN:
        ctx.warn("scale_suspicious", "The largest side is %.4f m, which is very small. "
                 "The file may use other units. Import with a scale (for example --scale 100)." % largest)


CHECKS.register("scale", run)
