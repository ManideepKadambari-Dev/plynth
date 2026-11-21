import os

from . import Step
from .. import registry, scene
from .. import importers as importers_package
from ..jobs import WorkerError
from ..registry import CLEANUP_STEPS

ORDER = 10


# Empties the scene and reads the model file with the importer of its format.
def run(ctx):
    source = ctx.params["source"]
    path = source["file"]
    registry.discover(importers_package)
    importer = registry.IMPORTERS.get(source["format"])
    if importer is None:
        raise WorkerError("unsupported_format", "The file format '%s' is not supported." % source["format"])
    if not os.path.isfile(path):
        raise WorkerError("import_failed", "The file %s does not exist." % path)
    ctx.state["picked_file"] = None
    ctx.state["search_folders"] = [os.path.dirname(os.path.abspath(path))]
    scene.clear_scene()
    try:
        importer.load(ctx, path)
    except WorkerError:
        raise
    except Exception as error:
        raise WorkerError("import_failed", "Blender could not read %s: %s" % (os.path.basename(path), error))


CLEANUP_STEPS.register("import_source", Step(ORDER, "Reading the model", run))
