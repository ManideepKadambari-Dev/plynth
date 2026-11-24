import bpy

from . import Step, count, report
from ..registry import BUILD_STEPS

ORDER = 100


# Places the set dressing props of every floor as instances of their asset collections, named
# SM_<asset>_<room>_D<NN> with NN counting the props of each room from 01. A prop whose asset did not load gives
# dressing_failed and is left out.
def run(ctx):
    loaded = ctx.state.get("asset_collections", {})
    for floor in ctx.params["floors"]:
        numbers = {}
        for prop in floor.get("dressing", []):
            collection = loaded.get(prop["asset"])
            if collection is None:
                report(ctx, "dressing_failed", "warning", "The prop %s could not be placed: its asset %s did not load." % (
                    prop["id"], prop["asset"]), floor["id"], prop["room"])
                continue
            number = numbers.get(prop["room"], 0) + 1
            numbers[prop["room"]] = number
            empty = bpy.data.objects.new("SM_%s_%s_D%02d" % (prop["asset"], prop["room"], number), None)
            empty.empty_display_type = "PLAIN_AXES"
            empty.empty_display_size = 0.05
            empty.instance_type = "COLLECTION"
            empty.instance_collection = collection
            empty.location = tuple(prop["location"])
            empty.rotation_euler = (0.0, 0.0, prop["rotation_z"])
            empty["plynth_id"] = prop["room"]
            empty["plynth_kind"] = "dressing"
            empty["plynth_asset"] = prop["asset"]
            empty["plynth_rule"] = prop["rule"]
            ctx.state["collections"][floor["id"]].objects.link(empty)
            count(ctx, "dressing")


BUILD_STEPS.register("dressing", Step(ORDER, "Adding set dressing", run))
