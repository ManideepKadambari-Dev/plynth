import bpy

from . import Step
from .. import scene, support_finder
from ..registry import CLEANUP_STEPS

ORDER = 85


# Finds the flat places on the cleaned model where set dressing may put props, in the asset's own axes from its
# pivot, and keeps them for the cleanup output.
def run(ctx):
    bpy.context.view_layer.update()
    triangles = []
    for obj in scene.mesh_objects(scene.scene_objects()):
        matrix = obj.matrix_world
        mesh = obj.data
        mesh.calc_loop_triangles()
        for tri in mesh.loop_triangles:
            points = [matrix @ mesh.vertices[i].co for i in tri.vertices]
            triangles.append([(p.x, p.y, p.z) for p in points])
    ctx.state["supports"] = support_finder.find_supports(triangles)


CLEANUP_STEPS.register("measure_supports", Step(ORDER, "Measuring shelves and tops", run))
