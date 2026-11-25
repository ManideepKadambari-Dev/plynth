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


# Returns the classes of the property groups render engine add-ons attach to lights.
def light_setting_groups():
    names = {p.fixed_type.identifier for p in bpy.types.Light.bl_rna.properties if p.type == "POINTER"}
    return [c for c in bpy.types.PropertyGroup.__subclasses__() if c.bl_rna.identifier in names]


# Gives each light property group that lacks cast_shadow a stand in for it, because the FBX importer of Blender
# 5.2 still sets it and a file with lights could not be read back otherwise. Returns the groups it was added to.
def add_light_stand_ins():
    added = [c for c in light_setting_groups() if "cast_shadow" not in c.bl_rna.properties]
    for group in added:
        group.cast_shadow = bpy.props.BoolProperty()
    return added


# Reads one written file back, next to the scene, and returns (mesh and light objects, walls bounds or None).
# The objects are the ones the import added.
def read_back(entry, walls):
    before = snapshot()
    known = set(before["objects"])
    stand_ins = add_light_stand_ins()
    try:
        bpy.ops.import_scene.fbx(filepath=entry["path"])
    finally:
        for group in stand_ins:
            del group.cast_shadow
    bpy.context.view_layer.update()
    added = [o for o in bpy.data.objects if o.name not in known and o.type in ("MESH", "LIGHT")]
    found = [o for o in added if o.type == "MESH" and base_name(o.name) in walls]
    bounds = scene.world_bounds(found) if found else None
    count = len(added)
    restore(before)
    return count, bounds


# Reads every written FBX back into the scene and fails unless the walls, taken together over all files, have
# the bounds the plan gives and the number of objects in each file matches what was written. What the import
# added is removed again.
def run(ctx):
    findings = []
    expected = ctx.params.get("expected_bounds")
    tolerance = ctx.params["limits"]["bounds_tolerance"]
    walls = {o.name for o in ctx.state["meshes"] if o.get("plynth_kind") == "walls"}
    low = [float("inf")] * 3
    high = [float("-inf")] * 3
    with_walls = []
    for entry in ctx.state["written_files"]:
        try:
            count, bounds = read_back(entry, walls & set(entry["objects"]))
        except Exception as error:
            findings.append(finding("%s could not be read back: %s" % (entry["file"], error), entry["file"]))
            continue
        if count != len(entry["objects"]):
            findings.append(finding("%s holds %d objects, %d were written." % (entry["file"], count,
                                                                              len(entry["objects"])), entry["file"]))
        if bounds is not None:
            with_walls.append(entry["file"])
            low = [min(a, b) for a, b in zip(low, bounds[0])]
            high = [max(a, b) for a, b in zip(high, bounds[1])]
    if expected is not None and with_walls:
        label = ", ".join(with_walls)
        findings += [finding("%s: %s" % (label, text), with_walls[0])
                     for text in bound_differences((low, high), expected, tolerance)]
    if findings:
        return result("fail", "A written file does not match the scene.", findings)
    return result("pass", "The written files read back with the right size and object count.")


QUALITY_CHECKS.register("fbx_scale", QualityCheck(ORDER, "file", "Written files match", run))
