import json
import os

from . import ImporterEntry
from . import zip as zip_importer
from ..jobs import WorkerError
from ..registry import IMPORTERS
from ..stores import fab_materials

MODEL_FORMATS = ("fbx", "gltf", "obj", "blend")
NORMAL_CHOICES = ("auto", "opengl", "directx")


# Reads source.fab.json and checks the fields this importer uses. Raises import_failed.
def read_source(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            source = json.load(handle)
    except (OSError, ValueError) as error:
        raise WorkerError("import_failed", "The Fab source file cannot be read: %s" % error)
    problem = None
    if not isinstance(source, dict) or source.get("format") != "plynth-fab-source" or source.get("version") != 1:
        problem = "is not a Fab source file"
    elif source.get("kind") != "model":
        problem = "is not for a model"
    elif not isinstance(source.get("zip"), str) or not isinstance(source.get("unpack_folder"), str):
        problem = "has no zip or unpack folder"
    elif not isinstance(source.get("files"), list) or not all(isinstance(e, str) for e in source["files"]):
        problem = "has no list of files"
    elif source.get("model") not in source["files"] or source.get("model_format") not in MODEL_FORMATS:
        problem = "names no model that it also lists"
    elif not isinstance(source.get("textures"), list) or not all(isinstance(e, str) for e in source["textures"]):
        problem = "has no list of textures"
    elif not isinstance(source.get("maps"), dict) or source.get("normal") not in NORMAL_CHOICES:
        problem = "has bad maps or normal fields"
    if problem:
        raise WorkerError("import_failed", "The Fab source file %s." % problem)
    return source


# Unpacks the entries the source lists, reads the model with the importer of its format, then builds the
# model's materials from the textures that came with it.
def load(ctx, path):
    source = read_source(path)
    folder = source["unpack_folder"]
    zip_importer.unpack(source["zip"], folder, source["files"])
    importer = IMPORTERS.get(source["model_format"])
    if importer is None:
        raise WorkerError("unsupported_format", "The file format '%s' is not supported." % source["model_format"])
    ctx.state["picked_file"] = source["model"]
    ctx.state.setdefault("search_folders", []).append(folder)
    importer.load(ctx, os.path.join(folder, *source["model"].split("/")))
    ctx.state["fab_materials"] = fab_materials.apply(ctx, source, folder)


IMPORTERS.register("fab", ImporterEntry((".fab.json",), load))
