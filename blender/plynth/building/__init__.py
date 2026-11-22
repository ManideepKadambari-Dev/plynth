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


# Returns the name of a slot: the library material id, or the neutral surface name when there is none. The
# first surface seen for a name is remembered so a material that fails to load can get a neutral look.
def slot_name(ctx, material, surface):
    name = material or surface
    ctx.state.setdefault("slot_surfaces", {}).setdefault(name, surface)
    return name
