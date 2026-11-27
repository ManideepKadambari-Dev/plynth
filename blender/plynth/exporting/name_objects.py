from . import Step, pascal, sanitize, strip_suffix, unique_name
from ..registry import EXPORT_STEPS

ORDER = 30
ENGINE_MESH_PREFIX = "SM_"
ENGINE_COLLISION_PREFIX = "UCX_"


# Works out a plain name for every building mesh: its own name with odd characters replaced, made unique.
# Returns a list of (object, new name).
def plan_plain_names(meshes):
    taken = set()
    return [(obj, unique_name(sanitize(obj.name), taken)) for obj in sorted(meshes, key=lambda o: o.name)]


# Works out the engine name of every mesh: SM_<name> for a building mesh, UCX_<its object>_<NN> for a collision
# mesh. Returns a list of (object, new name) and the new names by old name.
def plan_mesh_names(meshes, collision):
    taken = set()
    wanted = []
    for obj in sorted(meshes, key=lambda o: o.name):
        name = sanitize(obj.name)
        if not name.startswith(ENGINE_MESH_PREFIX):
            name = ENGINE_MESH_PREFIX + name
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
                if not name.startswith(ENGINE_COLLISION_PREFIX):
                    name = ENGINE_COLLISION_PREFIX + name
                wanted.append((obj, unique_name(name, taken)))
            else:
                wanted.append((obj, unique_name("%s%s_%02d" % (ENGINE_COLLISION_PREFIX, base, number), taken)))
    return wanted, new_of


# Renames the objects and their meshes in two passes, so a new name never meets an old one still in use, and
# points every collision mesh at its object's new name.
def rename_meshes(wanted, new_of):
    for number, (obj, _) in enumerate(wanted):
        obj.name = "__plynth_tmp_%d" % number
    for obj, new in wanted:
        obj.name = new
        if obj.data is not None:
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


# Gives every exported object and material the name the job's profile asks for.
def run(ctx):
    ctx.state["profile"].name_objects(ctx)


EXPORT_STEPS.register("name_objects", Step(ORDER, "Naming objects and materials", run))
