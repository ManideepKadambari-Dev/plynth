from . import QualityCheck, finding, result
from ._common import NAME_PATTERN, plan_id
from ..registry import QUALITY_CHECKS

ORDER = 50


# Returns the findings about the start of one mesh name under the profile's rules: the start it must have, and
# the starts it must not have.
def prefix_findings(kind, obj, wanted, refused):
    name = obj.name
    found = []
    if wanted and not name.startswith(wanted):
        found.append(finding("%s %s does not start with %s." % (kind, name, wanted), name, plan_id(obj)))
    for prefix in refused:
        if name.startswith(prefix):
            found.append(finding("%s %s starts with %s, which game engines read as their own naming. Rename it in "
                                 "building.blend." % (kind, name, prefix), name, plan_id(obj)))
    return found


# Fails for a mesh name that breaks the profile's rules (the unreal profile asks for SM_ and UCX_, the plain one
# refuses both), a repeated name, a character other than letters, digits and "_", a collision mesh whose object
# is not exported, and a material or texture name breaking the same rules.
def run(ctx):
    profile = ctx.state["profile"]
    findings = []
    meshes = ctx.state["meshes"]
    collision = ctx.state["collision"]
    mesh_names = {o.name for o in meshes}
    seen = {}
    for kind, wanted, objects in (("Building mesh", profile.mesh_prefix, meshes),
                                  ("Collision mesh", profile.collision_prefix, collision)):
        for obj in objects:
            name = obj.name
            findings += prefix_findings(kind, obj, wanted, profile.refused_prefixes)
            if not NAME_PATTERN.fullmatch(name):
                findings.append(finding("%s has a character other than letters, digits and underscore." % name,
                                        name, plan_id(obj)))
            key = name.lower()
            if key in seen:
                findings.append(finding("The name %s is used twice (%s)." % (name, seen[key]), name, plan_id(obj)))
            seen[key] = name
    for obj in collision:
        target = obj.get("plynth_collision_for")
        if target not in mesh_names:
            findings.append(finding("%s belongs to %s, which is not exported." % (obj.name, target or "nothing"),
                                    obj.name, plan_id(obj)))
    for name in sorted(ctx.state.get("materials", {})):
        if not NAME_PATTERN.fullmatch(name):
            findings.append(finding("The material slot name %s has a character other than letters, digits and "
                                    "underscore." % name, name))
    textures = set()
    for entry in ctx.state.get("texture_plan", []):
        stem, _, extension = entry["name"].rpartition(".")
        if not NAME_PATTERN.fullmatch(stem) or not extension.isalnum():
            findings.append(finding("The texture name %s has a character other than letters, digits and "
                                    "underscore." % entry["name"], entry["name"]))
        if entry["name"].lower() in textures:
            findings.append(finding("The texture name %s is used twice." % entry["name"], entry["name"]))
        textures.add(entry["name"].lower())
    if findings:
        return result("fail", "Some names break the naming rules of %s." % profile.name, findings)
    return result("pass", "Every object, slot and texture name follows the naming rules.")


QUALITY_CHECKS.register("naming", QualityCheck(ORDER, "scene", "Naming", run))
