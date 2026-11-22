import time

from .. import building as building_package
from .. import registry
from ..jobs import WorkerError, progress
from ..registry import TASKS


# Checks the params of the job. A missing or wrong field is a bad_job.
def check_params(params):
    for key in ("output_folder", "blend_file"):
        if not isinstance(params.get(key), str) or not params[key]:
            raise WorkerError("bad_job", "The job has no %s." % key)
    if params.get("fbx_file") is not None and not isinstance(params.get("fbx_file"), str):
        raise WorkerError("bad_job", "The fbx_file must be a name or null.")
    for key in ("settings", "plan"):
        if not isinstance(params.get(key), dict):
            raise WorkerError("bad_job", "The job has no %s." % key)
    for key in ("assets", "floors", "slots"):
        if not isinstance(params.get(key), list):
            raise WorkerError("bad_job", "The job has no %s list." % key)


# Builds the building the job describes: runs every registered build step in order and reports what was made.
def run(ctx):
    check_params(ctx.params)
    registry.discover(building_package)
    steps = sorted(registry.BUILD_STEPS.all(), key=lambda pair: pair[1].order)
    ctx.state["problems"] = []
    ctx.state["counts"] = {"wall_solids": 0, "openings_cut": 0, "fills": 0, "rooms": 0, "items": 0, "materials": 0,
                            "collision": 0}
    timings = []
    for index, (step_id, step) in enumerate(steps, start=1):
        progress(step_id, index, len(steps), step.message)
        ctx.state["current_step"] = step_id
        started = time.monotonic()
        step.run(ctx)
        timings.append({"step": step_id, "ms": int((time.monotonic() - started) * 1000)})

    ctx.output = {
        "blend_file": ctx.params["blend_file"],
        "fbx_file": ctx.state.get("fbx_file"),
        "materials_file": ctx.state.get("materials_file"),
        "objects": ctx.state.get("objects", 0),
        "triangles": ctx.state.get("triangles", 0),
        "counts": ctx.state["counts"],
        "steps": timings,
        "problems": ctx.state["problems"],
    }


TASKS.register("build", run)
