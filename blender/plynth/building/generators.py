import bpy

from . import Step, count, report
from .. import generating
from ..generating import addons, finish
from ..registry import BUILD_STEPS

ORDER = 70
TOLERANCE = 0.01


# Runs the generator of every generator item, joins what it makes into one mesh and puts it at the item's place.
# A generator whose add-on cannot be enabled is left out with a warning, and one that fails with an error; the
# build carries on with the next item.
def run(ctx):
    floors = ctx.params["floors"]
    if not any(floor.get("generator_items") for floor in floors):
        return
    count(ctx, "generators", 0)
    files = {g["id"]: g for g in ctx.params.get("generators", [])}
    loaded = {}
    missing = {}
    for floor in floors:
        for item in floor.get("generator_items", []):
            name = item["generator"]
            if name not in loaded and name not in missing:
                load(files.get(name), name, loaded, missing)
            if name in missing:
                code, text = missing[name]
                report(ctx, code, "warning" if code == "generator_unavailable" else "error", text, floor["id"],
                       item["id"])
                continue
            build_item(ctx, floor, item, loaded[name])


# Loads one generator file and enables its add-ons. A problem is kept in missing as (code, text).
def load(entry, name, loaded, missing):
    if entry is None:
        missing[name] = ("generator_failed", "The generator %s is not in the job." % name)
        return
    for addon in entry.get("addons", []):
        if not addons.enable(addon):
            missing[name] = ("generator_unavailable",
                             "The generator %s needs the add-on %s, which could not be enabled." % (name, addon))
            return
    try:
        loaded[name] = generating.load_generator(entry["file"], name)
    except generating.GeneratorError as error:
        missing[name] = ("generator_failed", "The generator %s failed: %s" % (name, error))


# Makes one item: an empty collection, generate(gen), then the final object, then the cleaning.
def build_item(ctx, floor, item, generator):
    before = finish.snapshot()
    work = bpy.data.collections.new("plynth_generator_work")
    bpy.context.scene.collection.children.link(work)
    gen = generating.Gen(ctx, generator, item, work)
    obj = None
    try:
        result = generator.generate(gen)
        objects = list(result) if result else []
        obj, overshoot = finish.finish(ctx, gen, objects, item, ctx.state["collections"][floor["id"]], TOLERANCE)
        if obj is None:
            report(ctx, "generator_failed", "error",
                   "The generator %s made no mesh for %s." % (item["generator"], item["id"]), floor["id"], item["id"])
        else:
            count(ctx, "generators")
            if overshoot:
                report(ctx, "generator_size", "warning",
                       "%s reaches past its size: %s." % (obj.name, overshoot), floor["id"], item["id"])
    except generating.AddonUnavailable as error:
        report(ctx, "generator_unavailable", "warning", "%s: %s" % (item["generator"], error), floor["id"],
               item["id"])
    except Exception as error:
        report(ctx, "generator_failed", "error",
               "The generator %s failed for %s: %s" % (item["generator"], item["id"], generating.last_line(error)),
               floor["id"], item["id"])
    finish.clean(before, obj)


BUILD_STEPS.register("generators", Step(ORDER, "Running generators", run))
