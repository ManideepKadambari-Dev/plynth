import re

import bpy

from . import QualityCheck, finding, result
from .scale import bound_differences
from .. import scene
from ..registry import QUALITY_CHECKS

ORDER = 110
DATA_KINDS = ("objects", "meshes", "materials", "images", "cameras", "lights", "collections", "armatures")


# Returns the name without the number Blender adds to a repeated name.
def base_name(name):
    return re.sub(r"\.\d{3}$", "", name)


# Remembers the names of every data block of the kinds an import can add.
def snapshot():
    return {kind: set(getattr(bpy.data, kind).keys()) for kind in DATA_KINDS}


# Removes every data block that is not in the snapshot.
def restore(before):
    for kind in DATA_KINDS:
        collection = getattr(bpy.data, kind)
        for name in [n for n in collection.keys() if n not in before[kind]]:
            collection.remove(collection[name])


# Reads one written file back, next to the scene, and returns (mesh objects, walls bounds or None). The
# objects are the ones the import added.
def read_back(entry, walls):
    before = snapshot()
    known = set(before["objects"])
    bpy.ops.import_scene.fbx(filepath=entry["path"])
    bpy.context.view_layer.update()
    added = [o for o in bpy.data.objects if o.name not in known and o.type == "MESH"]
    found = [o for o in added if base_name(o.name) in walls]
    bounds = scene.world_bounds(found) if found else None
    count = len(added)
    restore(before)
    return count, bounds


# Reads every written FBX back into the scene and fails unless the walls have the bounds the plan gives and
# the number of objects matches what was written. What the import added is removed again.
def run(ctx):
    findings = []
    expected = ctx.params.get("expected_bounds")
    tolerance = ctx.params["limits"]["bounds_tolerance"]
    walls = {o.name for o in ctx.state["meshes"] if o.get("plynth_kind") == "walls"}
    for entry in ctx.state["written_files"]:
        try:
            count, bounds = read_back(entry, walls & set(entry["objects"]))
        except Exception as error:
            findings.append(finding("%s could not be read back: %s" % (entry["file"], error), entry["file"]))
            continue
        if count != len(entry["objects"]):
            findings.append(finding("%s holds %d objects, %d were written." % (entry["file"], count,
                                                                              len(entry["objects"])), entry["file"]))
        if expected is not None and bounds is not None:
            findings += [finding("%s: %s" % (entry["file"], text), entry["file"])
                         for text in bound_differences(bounds, expected, tolerance)]
    if findings:
        return result("fail", "A written file does not match the scene.", findings)
    return result("pass", "The written files read back with the right size and object count.")


QUALITY_CHECKS.register("fbx_scale", QualityCheck(ORDER, "file", "Written files match", run))
