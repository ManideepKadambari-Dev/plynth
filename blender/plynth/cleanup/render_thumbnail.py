import os

from . import Step
from .. import thumbnail
from ..jobs import WorkerError
from ..registry import CLEANUP_STEPS

ORDER = 110


# Renders thumb.png beside asset.blend. A failed render is reported in the result and does not fail the job.
def run(ctx):
    spec = ctx.params["thumbnail"]
    if not spec.get("render", True):
        return
    target = os.path.join(ctx.params["output_folder"], "thumb.png")
    try:
        engine = thumbnail.render_thumbnail(target, spec, ctx.warn)
        ctx.state["thumbnail"] = {"file": "thumb.png", "engine": engine, "error": None}
    except WorkerError as error:
        ctx.state["thumbnail"] = {"file": None, "engine": None, "error": error.message}


CLEANUP_STEPS.register("render_thumbnail", Step(ORDER, "Rendering the thumbnail", run))
