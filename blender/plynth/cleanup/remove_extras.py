import bpy

from . import Step
from .. import scene
from ..jobs import WorkerError
from ..registry import CLEANUP_STEPS

ORDER = 20
REMOVED_KINDS = {"CAMERA": "camera", "LIGHT": "light", "EMPTY": "empty"}


# Turns curves and text into meshes, bakes modifiers, and removes cameras, lights, empties and anything
# that is not a mesh. Objects keep their place when their parent goes. Raises no_geometry when no mesh is left.
def run(ctx):
    bpy.context.view_layer.update()
    objects = scene.scene_objects()
    scene.unparent(objects)
    bpy.context.view_layer.update()
    scene.convert_to_meshes(scene.scene_objects())
    scene.bake_modifiers(scene.scene_objects())

    counts = {}
    other = []
    for obj in scene.scene_objects():
        keep = obj.type == "MESH" and len(obj.data.vertices) > 0
        if keep:
            continue
        if obj.type in REMOVED_KINDS:
            counts[REMOVED_KINDS[obj.type]] = counts.get(REMOVED_KINDS[obj.type], 0) + 1
        elif obj.type == "MESH":
            counts["empty mesh"] = counts.get("empty mesh", 0) + 1
        else:
            other.append("%s (%s)" % (obj.name, obj.type.lower()))
        bpy.data.objects.remove(obj, do_unlink=True)

    if counts:
        parts = ["%d %s" % (count, name) for name, count in sorted(counts.items())]
        ctx.warn("removed_objects", "Removed " + ", ".join(parts) + ".")
    if other:
        ctx.warn("unsupported_object", "Removed objects that cannot be used: " + ", ".join(sorted(other)) + ".")
    if not scene.mesh_objects(scene.scene_objects()):
        raise WorkerError("no_geometry", "The file holds no model with geometry.")


CLEANUP_STEPS.register("remove_extras", Step(ORDER, "Removing cameras, lights and extras", run))
