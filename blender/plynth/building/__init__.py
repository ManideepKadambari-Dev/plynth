from collections import namedtuple

# What each build module registers: where it runs in the order, the sentence shown as progress, and a
# function run(ctx).
Step = namedtuple("Step", "order message run")


# Records a problem about one object. The step that is running is filled in. A step reports a problem and
# carries on with the next object.
def report(ctx, code, severity, message, floor=None, obj=None):
    ctx.state.setdefault("problems", []).append({
        "code": code,
        "severity": severity,
        "message": message,
        "floor": floor,
        "object": obj,
        "step": ctx.state.get("current_step", "build"),
    })


# Adds to one of the counts in the build output.
def count(ctx, name, amount=1):
    counts = ctx.state.setdefault("counts", {})
    counts[name] = counts.get(name, 0) + amount


# Returns the Blender material name of a face: the slot name the job gives it. The slot must be in the job's
# slots table; a name that is not there is kept and treated as a neutral Wall_Inside look.
def slot_name(ctx, slot):
    return slot


# Returns the slots table of the job as a dictionary from slot name to its entry.
def slot_table(ctx):
    return {entry["name"]: entry for entry in ctx.params.get("slots", [])}
