import os
import re

import bpy

from . import Step, count, report
from .. import meshes, shading
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
    ctx.state["asset_materials"] = [entry for asset in ctx.params["assets"] if asset["id"] in loaded
                                    for entry in prepare_materials(ctx, asset, loaded[asset["id"]])]
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


# Gives every material of an appended asset that the job lists the image maps of the job, so each one has image
# nodes with absolute full size paths whatever the asset file held. The appended material may carry a number
# suffix when another asset had the same name. Returns one entry for the materials file per material.
def prepare_materials(ctx, asset, collection):
    wanted = {entry["name"]: entry["maps"] for entry in asset.get("materials", [])}
    found = []
    for obj in collection.all_objects:
        if obj.type != "MESH":
            continue
        for mat in obj.data.materials:
            if mat is not None and mat not in found:
                found.append(mat)
    entries = []
    for mat in found:
        name = mat.name if mat.name in wanted else re.sub(r"\.\d{3}$", "", mat.name)
        if name not in wanted:
            continue
        maps = {k: (v.replace("\\", "/") if v else None) for k, v in wanted[name].items()}
        try:
            shading.apply_maps(mat, maps)
        except shading.MapError as error:
            report(ctx, "material_failed", "warning", "The material %s of asset %s could not be loaded: %s" % (
                name, asset["id"], error), None, None)
            continue
        entries.append({"name": mat.name, "material": None, "surface": None, "asset": asset["id"], "maps": maps,
                        "alpha_mode": "blend" if maps.get("opacity") else "opaque"})
    return entries


# Appends the collection of one asset from its blend file and makes its meshes safe for the 3D view, which an
# asset imported before that cleanup may still need. Returns the collection and an empty reason, or None and
# the reason.
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
    done = set()
    for obj in collection.all_objects:
        if obj.type == "MESH" and obj.data.name not in done:
            done.add(obj.data.name)
            meshes.make_viewer_safe(obj.data)
    return collection, ""


# Makes the instance empty of one item in the floor collection at the placement from the job.
def place(ctx, floor, item, collection):
    empty = bpy.data.objects.new("%s_%s" % (item["asset"], item["id"]), None)
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
