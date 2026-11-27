import math
import statistics

from . import PLAIN, UNREAL, QualityCheck, finding, result
from ._common import CONSTANT_SIZE, base_color_source, polygon_areas, polygon_materials
from ..registry import QUALITY_CHECKS

ORDER = 70


# Adds the UV area and the real area of the faces of one mesh to the totals of each material.
def collect(mesh, totals):
    areas = polygon_areas(mesh)
    if areas is None:
        return
    uv_area, area = areas
    indices = polygon_materials(mesh)
    for slot, material in enumerate(mesh.materials):
        if material is None:
            continue
        chosen = (indices == slot) & (uv_area > 0.0) & (area > 0.0)
        if not chosen.any():
            continue
        total = totals.setdefault(material.name, [0.0, 0.0])
        total[0] += float(uv_area[chosen].sum())
        total[1] += float(area[chosen].sum())


# Measures the pixels per metre of the base color of each material on the faces that use it. Images of
# kConstantMapSize pixels or less are flat colors and are skipped.
def run(ctx):
    limits = ctx.params["limits"]
    totals = {}
    for obj in ctx.state["meshes"]:
        collect(obj.data, totals)
    densities = {}
    for name, info in sorted(ctx.state["materials"].items()):
        kind, image = base_color_source(info["material"])
        if kind != "image" or max(image.size[0], image.size[1]) <= CONSTANT_SIZE or name not in totals:
            continue
        uv_area, area = totals[name]
        densities[name] = math.sqrt(uv_area * image.size[0] * image.size[1] / area)
    if not densities:
        return result("pass", "No material has an image large enough to measure.")
    median = statistics.median(densities.values())
    findings = []
    failed = False
    for name, density in sorted(densities.items()):
        text = "%s shows %.0f pixels per metre, the median is %.0f." % (name, density, median)
        if density < median * limits["texel_fail_ratio"]:
            failed = True
            findings.append(finding("%s It is far blurrier than the rest." % text, name))
        elif density < median * limits["texel_warning_ratio"]:
            findings.append(finding("%s It is blurrier than the rest." % text, name))
        elif density < limits["low_texel_density"]:
            findings.append(finding("%s That is below %d pixels per metre." % (text, limits["low_texel_density"]),
                                    name))
    if failed:
        return result("fail", "Some textures are much blurrier than the others.", findings)
    if findings:
        return result("warning", "Some textures are blurry.", findings)
    return result("pass", "The textures are about equally sharp (median %.0f pixels per metre)." % median)


QUALITY_CHECKS.register("texel_density", QualityCheck(ORDER, "scene", "Texture sharpness", run, (PLAIN, UNREAL)))
