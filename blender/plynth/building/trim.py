import bmesh

from . import Step, count, report, slot_name
from .. import meshes, sweep_mesh
from ..registry import BUILD_STEPS

ORDER = 90


# Builds every trim entry of every floor as one mesh object from its sweeps: skirting, cornices, casings, sills,
# railings and guards. A sweep that cannot be skinned gives trim_failed and the build goes on.
def run(ctx):
    for floor in ctx.params["floors"]:
        for entry in floor.get("trim", []):
            try:
                made = make_trim(ctx, floor, entry)
            except Exception as error:
                report(ctx, "trim_failed", "warning", "The %s %s could not be built: %s" % (
                    entry.get("kind", "trim"), entry.get("object", ""), error), floor["id"], entry.get("plan_id"))
                continue
            if made:
                count(ctx, "trim")


# Makes the object of one trim entry, named as the job says. Returns false when it has no sweep left.
def make_trim(ctx, floor, entry):
    bm = bmesh.new()
    try:
        uv = meshes.uv_layer(bm)
        faces = 0
        for sweep in entry["sweeps"]:
            mesh = sweep_mesh.sweep_mesh(sweep["sections"], bool(sweep["closed"]))
            verts = [bm.verts.new(p) for p in mesh["vertices"]]
            for face, corners in zip(mesh["faces"], mesh["uvs"]):
                made = bm.faces.new([verts[i] for i in face])
                made.material_index = 0
                for loop, (u, v) in zip(made.loops, corners):
                    loop[uv].uv = (u, v)
                faces += 1
        if faces == 0:
            bm.free()
            return False
        collection = ctx.state["collections"][floor["id"]]
        obj = meshes.make_object(entry["object"], bm, collection, entry["plan_id"], entry["plynth_kind"])
    except Exception:
        if bm.is_valid:
            bm.free()
        raise
    meshes.add_slots(obj, [slot_name(ctx, entry["slot"])])
    obj["plynth_trim"] = entry["kind"]
    return True


BUILD_STEPS.register("trim", Step(ORDER, "Adding trim", run))
