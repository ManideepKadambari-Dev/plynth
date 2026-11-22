from .. import cleanup as cleanup_package
from .. import registry
from ..jobs import WorkerError, progress
from ..registry import TASKS

SOURCE_FORMATS = ("blend", "fbx", "obj", "gltf", "zip")
BAKE_SIZES = (256, 512, 1024, 2048, 4096)


# Checks the params of the job and fills in nothing: a missing or wrong field is a bad_job.
def check_params(params):
    source = params.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("file"), str) or not source.get("file"):
        raise WorkerError("bad_job", "The job has no source file.")
    if not isinstance(source.get("format"), str):
        raise WorkerError("bad_job", "The job has no source format.")
    for key in ("asset_id", "output_folder"):
        if not isinstance(params.get(key), str) or not params[key]:
            raise WorkerError("bad_job", "The job has no %s." % key)
    scale = params.get("scale", 1.0)
    if isinstance(scale, bool) or not isinstance(scale, (int, float)) or scale <= 0:
        raise WorkerError("bad_job", "The scale must be above zero.")
    if params.get("rotate_z", 0) not in (0, 90, 180, 270):
        raise WorkerError("bad_job", "The rotation must be 0, 90, 180 or 270.")
    if not isinstance(params.get("triangle_warning"), int):
        raise WorkerError("bad_job", "The job has no triangle warning limit.")
    if params.get("bake_size", 1024) not in BAKE_SIZES:
        raise WorkerError("bad_job", "The bake size must be 256, 512, 1024, 2048 or 4096.")
    if not isinstance(params.get("thumbnail"), dict):
        raise WorkerError("bad_job", "The job has no thumbnail settings.")


# Cleans a model to the asset conventions, saves it, measures it and renders its thumbnail.
def run(ctx):
    check_params(ctx.params)
    registry.discover(cleanup_package)
    steps = sorted(registry.CLEANUP_STEPS.all(), key=lambda pair: pair[1].order)
    ctx.state["thumbnail"] = {"file": None, "engine": None, "error": None}
    wants_thumbnail = bool(ctx.params["thumbnail"].get("render", True))
    if wants_thumbnail and registry.CLEANUP_STEPS.get("render_thumbnail") is None:
        ctx.state["thumbnail"]["error"] = "Thumbnail step not installed"

    for index, (step_id, step) in enumerate(steps, start=1):
        progress(step_id, index, len(steps), step.message)
        step.run(ctx)

    ctx.output = {
        "blend_file": "asset.blend",
        "fbx_file": ctx.state.get("fbx_file"),
        "picked_file": ctx.state.get("picked_file"),
        "size": ctx.state["size"],
        "triangles": ctx.state["triangles"],
        "objects": ctx.state["objects"],
        "materials": ctx.state["materials"],
        "textures": ctx.state["textures"],
        "missing_textures": ctx.state["missing_textures"],
        "material_maps": ctx.state.get("material_maps", []),
        "thumbnail": ctx.state["thumbnail"],
    }


TASKS.register("cleanup_asset", run)
