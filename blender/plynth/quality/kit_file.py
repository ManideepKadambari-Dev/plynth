import bpy

from . import UNREAL, QualityCheck, finding, result
from .fbx_scale import base_name, restore, snapshot
from .transforms import is_identity
from .. import scene
from ..registry import QUALITY_CHECKS

ORDER = 120
SLACK = 0.02


# Returns the (low, high) corners of the solids of a job piece in its own frame.
def piece_extent(piece):
    xs = [x for solid in piece["wall_solids"] for x, _ in solid["outline"]["points"]]
    ys = [y for solid in piece["wall_solids"] for _, y in solid["outline"]["points"]]
    top = max(solid["height"] for solid in piece["wall_solids"])
    return [min(xs), min(ys), 0.0], [max(xs), max(ys), top]


# Reads the kit file into the scene next to the existing objects and returns the mesh objects the import
# added. The caller removes them again.
def read_kit(path):
    known = set(bpy.data.objects.keys())
    bpy.ops.import_scene.fbx(filepath=path)
    bpy.context.view_layer.update()
    return [o for o in bpy.data.objects if o.name not in known and o.type == "MESH"]


# Fails unless the kit file holds every piece mesh once with an identity transform and its lowest corner inside
# the piece's own extent, and at least one UCX_ mesh for every piece. The import is removed again.
def run(ctx):
    kit = ctx.params.get("kit")
    path = ctx.state.get("kit_path")
    if not kit or not ctx.state.get("written") or not path:
        return result("skipped", "There is no kit file, so it was not checked.")
    findings = []
    before = snapshot()
    try:
        added = read_kit(path)
        found = {}
        for obj in added:
            found.setdefault(base_name(obj.name), []).append(obj)
        for piece in kit["pieces"]:
            mesh = piece["mesh"]
            objects = found.get(mesh, [])
            if len(objects) != 1:
                findings.append(finding("The kit file holds %s %d times." % (mesh, len(objects)), mesh, piece["id"]))
                continue
            obj = objects[0]
            if not is_identity(obj.matrix_world):
                findings.append(finding("%s is moved, turned or scaled in the kit file." % mesh, mesh, piece["id"]))
            bounds = scene.world_bounds([obj])
            low, high = piece_extent(piece)
            for axis in range(3):
                if not low[axis] - SLACK <= bounds[0][axis] <= high[axis] + SLACK:
                    findings.append(finding("The lowest corner of %s is outside the piece's own extent." % mesh,
                                            mesh, piece["id"]))
                    break
            prefix = "UCX_%s_" % mesh
            if not any(base_name(name).startswith(prefix) for name in found):
                findings.append(finding("%s has no UCX_ collision mesh in the kit file." % mesh, mesh, piece["id"]))
    except Exception as error:
        findings.append(finding("The kit file could not be read back: %s" % error))
    finally:
        restore(before)
    if findings:
        return result("fail", "The kit file does not match the kit.", findings)
    return result("pass", "The kit file holds every piece once, at its own origin, with collision.")


QUALITY_CHECKS.register("kit_file", QualityCheck(ORDER, "file", "Modular kit", run, (UNREAL,)))
