from . import QualityCheck, finding, result
from ._common import plan_id
from ..exporting.groups import assign
from ..registry import QUALITY_CHECKS

ORDER = 105


# Fails for every building object no group takes and warns for every group that gets no object, so nothing is
# left out of the export without a word. Uses the same assignment as the writer.
def run(ctx):
    groups = ctx.params["groups"]
    found, unassigned = assign(groups, ctx.state["meshes"], ctx.state["collision"])
    problems = [finding("%s is in no group, so no file would hold it." % o.name, o.name, plan_id(o))
                for o in unassigned]
    if problems:
        return result("fail", "Some objects belong to no group.", problems)
    empty = [finding("The group %s gets no object, so its file is not written." % g["id"])
             for g in groups if not found[g["id"]]]
    if empty:
        return result("warning", "Some groups have no objects.", empty)
    return result("pass", "Every object is in a group and every group has objects.")


QUALITY_CHECKS.register("groups", QualityCheck(ORDER, "scene", "Groups", run))
