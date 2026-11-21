import os

import bpy

from .. import scene
from .. import thumbnail
from ..jobs import WorkerError
from ..registry import TASKS


# Returns the image files the model uses, relative to the folder of the blend file, with forward slashes.
def texture_paths(blend_file):
    folder = os.path.dirname(os.path.abspath(blend_file))
    found = set()
    for image in bpy.data.images:
        if image.source != "FILE" or not image.filepath or image.users == 0:
            continue
        absolute = os.path.abspath(bpy.path.abspath(image.filepath))
        found.add(os.path.relpath(absolute, folder).replace("\\", "/"))
    return sorted(found)


# Checks the params of the job. A missing or wrong field is a bad_job.
def check_params(params):
    for key in ("blend_file", "output_file"):
        if not isinstance(params.get(key), str) or not params[key]:
            raise WorkerError("bad_job", "The job has no %s." % key)
    if not isinstance(params.get("measure"), bool):
        raise WorkerError("bad_job", "The job does not say whether to measure.")
    if not isinstance(params.get("thumbnail"), dict):
        raise WorkerError("bad_job", "The job has no thumbnail settings.")


# Opens an asset.blend without saving it, measures it when asked, and renders its thumbnail.
def run(ctx):
    check_params(ctx.params)
    blend_file = ctx.params["blend_file"]
    output_file = ctx.params["output_file"]
    if not os.path.isfile(blend_file):
        raise WorkerError("import_failed", "The file %s does not exist." % blend_file)
    try:
        bpy.ops.wm.open_mainfile(filepath=blend_file, load_ui=False)
    except Exception as error:
        raise WorkerError("import_failed", "%s cannot be opened: %s" % (os.path.basename(blend_file), error))
    objects = scene.mesh_objects(scene.scene_objects())
    if not objects:
        raise WorkerError("no_geometry", "The file holds no model with geometry.")

    output = {"size": None, "triangles": None, "objects": None, "materials": None, "textures": None}
    if ctx.params["measure"]:
        output.update(scene.describe(objects))
        output["textures"] = texture_paths(blend_file)

    spec = ctx.params["thumbnail"]
    if spec.get("render", True):
        engine = thumbnail.render_thumbnail(output_file, spec, ctx.warn)
        output["thumbnail"] = {"file": os.path.basename(output_file), "engine": engine, "error": None}
    else:
        output["thumbnail"] = {"file": None, "engine": None, "error": None}
    ctx.output = output


TASKS.register("render_thumbnail", run)
