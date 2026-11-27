from .. import ue
from ..registry import check


# Checks every static mesh of the job, the kit pieces too, has simple collision from its UCX_ meshes.
@check("collision", 30)
def run(ctx):
    missing = []
    paths = ctx.all_mesh_paths()
    for path in paths:
        mesh = ue.load(path)
        if mesh is None or ue.mesh_tools().get_simple_collision_count(mesh) <= 0:
            missing.append(path.rsplit("/", 1)[-1])
    if missing:
        return "fail", "%d of %d meshes have no collision: %s." % (len(missing), len(paths), ", ".join(missing[:10]))
    return "pass", "All %d meshes have simple collision." % len(paths)
