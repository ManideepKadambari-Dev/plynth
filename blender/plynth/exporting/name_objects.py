from . import Step, pascal, sanitize, strip_suffix, unique_name
from ..registry import EXPORT_STEPS

ORDER = 30


# Works out the new name of every mesh: SM_<name> for a building mesh, UCX_<its object>_<NN> for a collision
# mesh. Returns a list of (object, new name).
def plan_mesh_names(meshes, collision):
    taken = set()
    wanted = []
    for obj in sorted(meshes, key=lambda o: o.name):
        name = sanitize(obj.name)
        if not name.startswith("SM_"):
            name = "SM_" + name
        wanted.append((obj, unique_name(name, taken)))
    new_of = {obj.name: new for obj, new in wanted}
    groups = {}
    for obj in sorted(collision, key=lambda o: o.name):
        groups.setdefault(obj.get("plynth_collision_for"), []).append(obj)
    for target, members in groups.items():
        base = new_of.get(target)
        for number, obj in enumerate(members):
            if base is None:
                name = sanitize(obj.name)
                wanted.append((obj, unique_name(name if name.startswith("UCX_") else "UCX_" + name, taken)))
            else:
                wanted.append((obj, unique_name("UCX_%s_%02d" % (base, number), taken)))
    return wanted, new_of


# Renames the meshes in two passes, so a new name never meets an old one still in use.
def rename_meshes(wanted, new_of):
    old_names = {}
    for number, (obj, _) in enumerate(wanted):
        old_names[obj] = obj.name
        obj.name = "__plynth_tmp_%d" % number
    for obj, new in wanted:
        obj.name = new
        obj.data.name = new
        target = obj.get("plynth_collision_for")
        if target is not None and target in new_of:
            obj["plynth_collision_for"] = new_of[target]


# Renames the materials the meshes use. A furniture material becomes <Asset>_<name>, so two assets never share
# a name. Every other name only has its odd characters replaced. Returns {final name: info} where info holds
# the material, its original name and its entry of the job's materials list (or None).
def name_materials(ctx, meshes):
    entries = {entry["name"]: entry for entry in ctx.params.get("materials", []) if "name" in entry}
    used = []
    for obj in meshes:
        for mat in obj.data.materials:
            if mat is not None and mat not in used:
                used.append(mat)
    taken = set()
    plan = []
    for mat in sorted(used, key=lambda m: m.name):
        entry = entries.get(mat.name)
        if entry is not None and entry.get("asset"):
            name = "%s_%s" % (pascal(entry["asset"]), sanitize(strip_suffix(mat.name)))
        else:
            name = sanitize(mat.name)
        plan.append((mat, mat.name, entry, unique_name(name, taken)))
    for number, (mat, _, _, _) in enumerate(plan):
        mat.name = "__plynth_mat_%d" % number
    info = {}
    for mat, original, entry, final in plan:
        mat.name = final
        info[final] = {"material": mat, "original": original, "entry": entry}
    return info


# Gives every exported object and material its Unreal name.
def run(ctx):
    wanted, new_of = plan_mesh_names(ctx.state["meshes"], ctx.state["collision"])
    rename_meshes(wanted, new_of)
    ctx.state["materials"] = name_materials(ctx, ctx.state["meshes"])


EXPORT_STEPS.register("name_objects", Step(ORDER, "Naming objects and materials", run))
