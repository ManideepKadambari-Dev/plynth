from .. import ue
from ..registry import check
from ..steps.level import actor_mesh_path, has_tag


# Checks the level holds one actor with the building's tag for each mesh of the job, and nothing else tagged.
@check("actors", 60)
def run(ctx):
    tag = ctx.job["level"]["tag"]
    ours = [actor for actor in ue.actors().get_all_level_actors() if has_tag(actor, tag)]
    if not ctx.job["level"]["place_actors"]:
        return "pass", "The job did not ask for actors; %d actors carry the tag %s." % (len(ours), tag)
    problems = []
    for mesh in ctx.job["meshes"]:
        path = ctx.mesh_path(mesh["asset"])
        count = sum(1 for actor in ours if actor_mesh_path(actor) == path)
        if count != 1:
            problems.append("%s has %d actors" % (mesh["asset"], count))
    extra = len(ours) - len(ctx.job["meshes"])
    if extra > 0:
        problems.append("%d more actors carry the tag" % extra)
    if problems:
        return "fail", "%s." % "; ".join(problems[:10])
    return "pass", "The level has one actor tagged %s for each of the %d meshes." % (tag, len(ctx.job["meshes"]))
