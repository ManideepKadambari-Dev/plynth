import bpy

from . import Step, count, report
from ..registry import BUILD_STEPS

ORDER = 65


# Places the catalog door or window of every opening that names one, as an instance of the asset's collection
# at the placement core worked out. An asset that did not load keeps the fill Plynth built and gives a warning.
def run(ctx):
    loaded = ctx.state.get("asset_collections", {})
    for floor in ctx.params["floors"]:
        for opening in floor.get("openings", []):
            asset = opening.get("asset")
            if not asset:
                continue
            collection = loaded.get(asset["id"])
            if collection is None:
                report(ctx, "fill_asset_failed", "warning",
                       "The %s asset %s could not be loaded, so the built fill is used." % (opening.get("type", "opening"),
                                                                                           asset["id"]),
                       floor["id"], opening["id"])
                continue
            try:
                place(ctx, floor, opening, asset, collection)
            except Exception as error:
                report(ctx, "fill_asset_failed", "warning", "The asset of opening %s could not be placed: %s" % (
                    opening["id"], error), floor["id"], opening["id"])
                continue
            count(ctx, "opening_assets")


# Makes the instance empty of one opening's asset: location, turn about Z and scale from the job. A negative x
# scale mirrors a door that hangs on the other jamb.
def place(ctx, floor, opening, asset, collection):
    empty = bpy.data.objects.new("SM_%s_%s" % (asset["id"], opening["id"]), None)
    empty.empty_display_type = "PLAIN_AXES"
    empty.empty_display_size = 0.1
    empty.instance_type = "COLLECTION"
    empty.instance_collection = collection
    empty.location = tuple(asset["location"])
    empty.rotation_euler = (0.0, 0.0, asset["rotation_z"])
    empty.scale = tuple(asset["scale"])
    empty["plynth_id"] = opening["id"]
    empty["plynth_kind"] = "window" if opening.get("type") == "window" else "door"
    empty["plynth_asset"] = asset["id"]
    ctx.state["collections"][floor["id"]].objects.link(empty)
    ctx.state.setdefault("opening_assets", {})[opening["id"]] = empty.name


BUILD_STEPS.register("opening_assets", Step(ORDER, "Fitting catalog doors and windows", run))
