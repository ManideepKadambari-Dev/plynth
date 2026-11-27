import time

from .. import exporting as exporting_package
from .. import registry
from ..exporting import profiles as profiles_package
from ..jobs import WorkerError, progress
from ..registry import EXPORT_PROFILES, TASKS


# Checks the params of the job. A missing or wrong field, or a profile no module registered, is a bad_job.
def check_params(params):
    profile_id = params.get("profile")
    if not isinstance(profile_id, str) or EXPORT_PROFILES.get(profile_id) is None:
        raise WorkerError("bad_job", "The job has no known export profile (%s)." % profile_id)
    for key in ("blend_file", "output_folder", "textures_folder"):
        if not isinstance(params.get(key), str) or not params[key]:
            raise WorkerError("bad_job", "The job has no %s." % key)
    for key in ("groups", "materials"):
        if not isinstance(params.get(key), list):
            raise WorkerError("bad_job", "The job has no %s list." % key)
    if not params["groups"]:
        raise WorkerError("bad_job", "The job has no groups.")
    for group in params["groups"]:
        if not isinstance(group, dict) or not isinstance(group.get("file"), str) or not group["file"]:
            raise WorkerError("bad_job", "A group has no file name.")
        for key in ("kinds", "ids"):
            if not isinstance(group.get(key), list):
                raise WorkerError("bad_job", "A group has no %s list." % key)
        for member in group.get("members") or []:
            if not isinstance(member, dict) or not isinstance(member.get("kind"), str) or "id" not in member:
                raise WorkerError("bad_job", "A group has a member without a kind and an id.")
        if not isinstance(group.get("rest", False), bool):
            raise WorkerError("bad_job", "A group has a rest switch that is not true or false.")
    kit = params.get("kit")
    if kit is not None and not isinstance(kit, dict):
        raise WorkerError("bad_job", "The job has a kit that is not an object.")
    for key in ("build", "limits"):
        if not isinstance(params.get(key), dict):
            raise WorkerError("bad_job", "The job has no %s." % key)
    for key in ("force", "write"):
        if not isinstance(params.get(key), bool):
            raise WorkerError("bad_job", "The job has no %s switch." % key)


# Exports the last build with the job's profile: runs every registered export step in order and reports what
# was written.
def run(ctx):
    registry.discover(profiles_package)
    check_params(ctx.params)
    ctx.state["profile_id"] = ctx.params["profile"]
    ctx.state["profile"] = EXPORT_PROFILES.get(ctx.params["profile"])
    registry.discover(exporting_package)
    steps = sorted(registry.EXPORT_STEPS.all(), key=lambda pair: pair[1].order)
    ctx.state["written"] = False
    timings = []
    for index, (step_id, step) in enumerate(steps, start=1):
        progress(step_id, index, len(steps), step.message)
        started = time.monotonic()
        step.run(ctx)
        timings.append({"step": step_id, "ms": int((time.monotonic() - started) * 1000)})

    meshes = [o for o in ctx.state["meshes"] if o.get("plynth_kind") != "kit"]
    collision = [o for o in ctx.state["collision"] if o.get("plynth_kind") != "kit"]
    written = ctx.state["written"]
    ctx.output = {
        "written": written,
        "files": [{"group": f["group"], "file": f["file"], "objects": len(f["objects"]), "triangles": f["triangles"]}
                  for f in ctx.state["written_files"]],
        "textures": sorted(ctx.state["textures"]) if written else [],
        "objects": len(meshes) + len(collision),
        "triangles": sum(_triangles(o) for o in meshes),
        "collision_objects": len(collision),
        "empty_groups": list(ctx.state.get("empty_groups", [])),
        "kit": ctx.state.get("kit_output"),
        "checks": ctx.state.get("check_results", []),
        "steps": timings,
    }


# Counts the triangles of one mesh object.
def _triangles(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


TASKS.register("export", run)
