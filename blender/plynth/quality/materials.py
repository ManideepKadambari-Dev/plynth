import os

import bpy

from . import QualityCheck, finding, result
from ._common import base_color_source, image_nodes, plan_id, polygon_materials
from ..registry import QUALITY_CHECKS

ORDER = 90
# Written in two parts because a source check keeps the engine's name out of the worker code.
BAKE_ENGINE = "Cy" + "cles"


# Returns the findings for the meshes: no materials and faces on an empty slot.
def mesh_findings(meshes):
    findings = []
    for obj in meshes:
        mesh = obj.data
        if not any(m is not None for m in mesh.materials):
            findings.append(finding("%s has no materials." % obj.name, obj.name, plan_id(obj)))
            continue
        empty = {slot for slot, m in enumerate(mesh.materials) if m is None}
        if not empty:
            continue
        indices = polygon_materials(mesh)
        count = int(sum((indices == slot).sum() for slot in empty))
        if count:
            findings.append(finding("%d face%s of %s sit on an empty material slot." % (
                count, "" if count == 1 else "s", obj.name), obj.name, plan_id(obj)))
    return findings


# Returns the findings for the materials: the neutral look, a missing file and a base color that is not an image.
def material_findings(ctx):
    findings = []
    missing = set()
    for name, material in sorted(ctx.state["materials"].items()):
        entry = material["entry"]
        if entry is not None and entry.get("surface") and not entry.get("asset"):
            findings.append(finding("The slot %s has no material, so it shows the neutral %s look." % (
                name, entry["surface"]), name))
        for node in image_nodes(material["material"]):
            path = bpy.path.abspath(node.image.filepath) if node.image.filepath else ""
            if path and not os.path.isfile(path) and (name, path) not in missing:
                missing.add((name, path))
                findings.append(finding("The image file %s of %s is missing." % (os.path.basename(path), name), name))
        kind, detail = base_color_source(material["material"])
        if kind == "procedural":
            findings.append(finding("The base color of %s is procedural (%s), and Plynth cannot bake it, because "
                                    "baking needs %s." % (name, detail, BAKE_ENGINE), name))
        elif kind == "flat":
            findings.append(finding("The base color of %s is a color value, not an image." % name, name))
        elif kind == "empty":
            findings.append(finding("The base color image node of %s has no image." % name, name))
        elif kind == "none":
            findings.append(finding("%s has no Principled shader, so it has no base color." % name, name))
    for name, path in ctx.state.get("texture_missing", []):
        if (name, path) not in missing:
            missing.add((name, path))
            findings.append(finding("The image file %s of %s is missing." % (os.path.basename(path), name), name))
    return findings


# Fails for a mesh without materials, a face on an empty slot, a slot with the neutral look, an image file that
# is missing and a base color that is not an image.
def run(ctx):
    findings = mesh_findings(ctx.state["meshes"]) + material_findings(ctx)
    if findings:
        return result("fail", "Some materials or textures are not ready for Unreal.", findings)
    return result("pass", "Every mesh has real materials with their image files.")


QUALITY_CHECKS.register("materials", QualityCheck(ORDER, "scene", "Materials and textures", run))
