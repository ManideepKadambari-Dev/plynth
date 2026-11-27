import unreal

from .. import ue
from ..registry import check
from ..steps.slots import slot_name


# Checks every mesh's slot names are the job's and carry their material instance, or, without Pack with
# materials, that no slot points at a material in the building folder and no material asset is left there.
@check("material_slots", 50)
def run(ctx):
    expected = {entry["slot"].lower(): entry["asset"] for entry in ctx.job["materials"]}
    problems = []
    slots = 0
    for path in ctx.all_mesh_paths():
        mesh = ue.load(path)
        if mesh is None:
            problems.append("%s is missing" % path.rsplit("/", 1)[-1])
            continue
        for slot in mesh.get_editor_property("static_materials"):
            slots += 1
            name = slot_name(slot)
            material = ue.try_get(slot, "material_interface")
            material_path = ue.package_path(material.get_path_name()) if material is not None else ""
            if not ctx.pack:
                if material_path.startswith(ctx.content_path + "/"):
                    problems.append("%s slot %s still uses %s" % (mesh.get_name(), name, material_path))
                continue
            if name.lower() not in expected:
                problems.append("%s has the slot %s, which the job does not name" % (mesh.get_name(), name))
            elif material_path != ctx.materials_folder + "/" + expected[name.lower()]:
                problems.append("%s slot %s uses %s instead of %s" % (mesh.get_name(), name,
                                                                     material_path or "no material",
                                                                     expected[name.lower()]))
    if not ctx.pack:
        left = [path for path in ue.list_folder(ctx.content_path)
                if isinstance(ue.load(path), unreal.MaterialInterface)]
        if left:
            problems.append("%d material assets are still there" % len(left))
    if problems:
        return "fail", "%d slot problems: %s." % (len(problems), "; ".join(problems[:10]))
    if not ctx.pack:
        return "pass", "Pack with materials is off: %d slots use Unreal's default material." % slots
    return "pass", "All %d slots have the job's names and their material instances." % slots
