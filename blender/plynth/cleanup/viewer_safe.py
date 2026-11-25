import time

from . import Step
from .. import meshes, scene
from ..registry import CLEANUP_STEPS

ORDER = 75


# Removes loose points and edges and splits faces with more than four corners into triangles, on every mesh,
# so asset.fbx and the builds that place the asset load in the 3D view.
def run(ctx):
    started = time.monotonic()
    done = set()
    split = 0
    for obj in scene.mesh_objects(scene.scene_objects()):
        if obj.data.name in done:
            continue
        done.add(obj.data.name)
        split += meshes.make_viewer_safe(obj.data)
    print("Split %d large faces into triangles in %.2f s" % (split, time.monotonic() - started))


CLEANUP_STEPS.register("viewer_safe", Step(ORDER, "Splitting large faces", run))
