import bmesh

from . import Step
from ..registry import BUILD_STEPS

ORDER = 25
MAX_SIDES = 4


# Splits every wall face with more than four corners into triangles. The cuts leave large faces with a bridged
# hole, and the viewers that read the FBX cannot fill those correctly, so a wall face can vanish there.
def run(ctx):
    for walls in ctx.state.get("walls_objects", {}).values():
        bm = bmesh.new()
        bm.from_mesh(walls.data)
        big = [face for face in bm.faces if len(face.verts) > MAX_SIDES]
        if big:
            bmesh.ops.triangulate(bm, faces=big, quad_method="BEAUTY", ngon_method="EAR_CLIP")
            bm.to_mesh(walls.data)
        bm.free()


BUILD_STEPS.register("triangulate", Step(ORDER, "Triangulating large faces", run))
