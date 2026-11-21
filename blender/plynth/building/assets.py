import os

import bpy

from . import Step, count, report
from ..registry import BUILD_STEPS

ORDER = 60


# Appends every asset the items use once, then places each item as an instance of its asset collection. The
# appended collections stay out of the scene. An asset that cannot be loaded leaves out its items and each one
# is reported at its marker.
def run(ctx):
    loaded = {}
    failed = {}
    for asset in ctx.params["assets"]:
        collection, why = load_asset(asset)
        if collection is None:
            failed[asset["id"]] = why
        else:
            loaded[asset["id"]] = collection
    ctx.state["asset_collections"] = loaded
    for floor in ctx.params["floors"]:
        for item in floor["items"]:
            collection = loaded.get(item["asset"])
            if collection is None:
                why = failed.get(item["asset"], "The asset is not in the job.")
                report(ctx, "asset_load_failed", "error", "The asset %s could not be loaded: %s" % (item["asset"], why),
                       floor["id"], item["id"])
                continue
            place(ctx, floor, item, collection)
            count(ctx, "items")


# Appends the collection of one asset from its blend file. Returns the collection and an empty reason, or None
# and the reason.
def load_asset(asset):
    path = asset["blend_file"]
    if not os.path.isfile(path):
        return None, "%s does not exist." % os.path.basename(path)
    try:
        with bpy.data.libraries.load(path, link=False, relative=False) as (source, target):
            if asset["collection"] not in source.collections:
                return None, "it has no collection named %s." % asset["collection"]
            target.collections = [asset["collection"]]
    except Exception as error:
        return None, str(error)
    collection = target.collections[0]
    if collection is None:
        return None, "the collection could not be appended."
    return collection, ""


# Makes the instance empty of one item in the floor collection at the placement from the job.
def place(ctx, floor, item, collection):
    empty = bpy.data.objects.new("SM_%s_%s" % (item["asset"], item["id"]), None)
    empty.empty_display_type = "PLAIN_AXES"
    empty.empty_display_size = 0.1
    empty.instance_type = "COLLECTION"
    empty.instance_collection = collection
    empty.location = tuple(item["location"])
    empty.rotation_euler = (0.0, 0.0, item["rotation_z"])
    empty["plynth_id"] = item["id"]
    empty["plynth_kind"] = "item"
    empty["plynth_asset"] = item["asset"]
    ctx.state["collections"][floor["id"]].objects.link(empty)


BUILD_STEPS.register("assets", Step(ORDER, "Placing furniture", run))
