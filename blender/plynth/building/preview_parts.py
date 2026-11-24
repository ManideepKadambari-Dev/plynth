import json
import os

from . import Step, report
from .. import exporters
from ..exporters import fbx  # noqa: F401
from ..registry import BUILD_STEPS

ORDER = 135
HIDDEN_KINDS = ("roof", "collision", "light")


# Writes the preview parts: for every floor one FBX with everything of the floor but its slab and roofs, one with
# its slab and one with its roofs when it has any, then parts.json that lists them. Skipped when the job has no
# preview folder. When anything fails the folder keeps no parts.json, preview_failed is reported and the build
# carries on.
def run(ctx):
    name = ctx.params.get("preview_folder")
    if not name:
        return
    folder = os.path.join(ctx.params["output_folder"], name)
    try:
        write_parts(ctx, folder)
    except Exception as error:
        remove_listing(folder)
        report(ctx, "preview_failed", "warning", "The preview parts could not be written: %s" % error)


# Removes parts.json, so a folder with a broken set of parts is never read.
def remove_listing(folder):
    path = os.path.join(folder, "parts.json")
    if os.path.exists(path):
        os.remove(path)


# Returns the objects of the floor's collection as storey, slab and roof objects, each in name order.
def split_objects(collection):
    storey = []
    slabs = []
    roofs = []
    for obj in sorted(collection.objects, key=lambda o: o.name):
        kind = obj.get("plynth_kind")
        if kind == "roof":
            roofs.append(obj)
        elif kind == "slab":
            slabs.append(obj)
        elif kind not in HIDDEN_KINDS:
            storey.append(obj)
    return storey, slabs, roofs


# Writes every part file and parts.json into the folder.
def write_parts(ctx, folder):
    os.makedirs(folder, exist_ok=True)
    for old in os.listdir(folder):
        if old.endswith(".fbx") or old == "parts.json":
            os.remove(os.path.join(folder, old))
    parts = []
    for floor in ctx.params["floors"]:
        storey, slabs, roofs = split_objects(ctx.state["collections"][floor["id"]])
        for kind, objects, file in (("storey", storey, "%s.fbx" % floor["id"]),
                                    ("slab", slabs, "%s_slab.fbx" % floor["id"]),
                                    ("roof", roofs, "%s_roof.fbx" % floor["id"])):
            if not objects:
                continue
            exporters.write_file("fbx", objects, os.path.join(folder, file), "ABSOLUTE")
            parts.append({"floor": floor["id"], "kind": kind, "file": file})
    listing = os.path.join(folder, "parts.json")
    temporary = os.path.join(folder, "parts.part.json")
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump({"format": "plynth-build-preview", "version": 1, "parts": parts}, handle, indent=1)
    os.replace(temporary, listing)


BUILD_STEPS.register("preview_parts", Step(ORDER, "Writing the preview parts", run))
