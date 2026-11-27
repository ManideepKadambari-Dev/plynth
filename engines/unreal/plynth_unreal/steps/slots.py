from .. import ue
from ..registry import step


# Puts each slot's material instance on every mesh with Pack with materials, or Unreal's default material
# without it, so the meshes never point at materials that are about to be removed.
@step("slots", 80, "material_failed")
def run(ctx):
    for path in ctx.all_mesh_paths():
        mesh = ue.load(path)
        if mesh is None:
            continue
        changed = False
        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            wanted = None
            if ctx.pack:
                wanted = instance_for(ctx, slot)
                if wanted is None:
                    ue.warn("%s has the slot %s, which the job has no material for" % (path, slot_name(slot)))
                    continue
            current = ue.try_get(slot, "material_interface")
            if current != wanted:
                mesh.set_material(index, wanted)
                changed = True
        if changed:
            ctx.touched(mesh, path)


# Returns the name of a mesh's material slot.
def slot_name(slot):
    return str(ue.try_get(slot, "material_slot_name", ""))


# Returns the material instance for a mesh slot, by its slot name or the name it was imported with.
def instance_for(ctx, slot):
    for name in (slot_name(slot), str(ue.try_get(slot, "imported_material_slot_name", ""))):
        if name and name.lower() in ctx.instances:
            return ctx.instances[name.lower()]
    return None
