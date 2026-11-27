import numpy as np

from . import QualityCheck, finding, result
from ._common import base_color_source, plan_id, polygon_areas
from ..registry import QUALITY_CHECKS

ORDER = 60
EMPTY_UV_AREA = 1e-10
MIN_FACE_AREA = 1e-8
RESOLUTION = 256


# Says whether the mesh has a material fed by an image.
def is_textured(mesh):
    for material in mesh.materials:
        if material is not None and base_color_source(material)[0] in ("image", "empty"):
            return True
    return False


# Returns the UV triangles of a mesh as an array of shape (triangles, 3, 2), or None without a UV map.
def uv_triangles(mesh):
    layer = mesh.uv_layers.active or (mesh.uv_layers[0] if len(mesh.uv_layers) else None)
    if layer is None:
        return None
    mesh.calc_loop_triangles()
    count = len(mesh.loop_triangles)
    if count == 0:
        return None
    loops = np.empty(count * 3, dtype=np.int64)
    mesh.loop_triangles.foreach_get("loops", loops)
    uv = np.empty(len(mesh.loops) * 2, dtype=np.float64)
    layer.uv.foreach_get("vector", uv)
    return uv.reshape(-1, 2)[loops.reshape(-1, 3)]


# Says whether two UV triangles cover the same spot. Each triangle is tested at the centre of the pixels of a
# grid over the 0 to 1 square, and a pixel on a shared edge counts for neither, so triangles that only touch
# are not reported.
def has_overlap(triangles):
    grid = np.zeros((RESOLUTION, RESOLUTION), dtype=np.uint8)
    for a, b, c in triangles:
        low = np.floor(np.minimum(np.minimum(a, b), c) * RESOLUTION - 0.5).astype(int)
        high = np.ceil(np.maximum(np.maximum(a, b), c) * RESOLUTION - 0.5).astype(int)
        low = np.maximum(low, 0)
        high = np.minimum(high, RESOLUTION - 1)
        if high[0] < low[0] or high[1] < low[1]:
            continue
        det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(det) < 1e-14:
            continue
        xs = (np.arange(low[0], high[0] + 1) + 0.5) / RESOLUTION
        ys = (np.arange(low[1], high[1] + 1) + 0.5) / RESOLUTION
        px, py = np.meshgrid(xs, ys)
        w1 = ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / det
        w2 = ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / det
        w3 = 1.0 - w1 - w2
        inside = (w1 > 1e-9) & (w2 > 1e-9) & (w3 > 1e-9)
        if not inside.any():
            continue
        window = grid[low[1]:high[1] + 1, low[0]:high[0] + 1]
        window[inside] += 1
        if (window[inside] >= 2).any():
            return True
    return False


# Fails for a textured mesh without UVs. Warns for faces whose UVs have no area and for furniture whose UVs
# overlap. Building surfaces tile on purpose, so they are not checked for overlap.
def run(ctx):
    failures = []
    warnings = []
    for obj in ctx.state["meshes"]:
        mesh = obj.data
        if len(mesh.uv_layers) == 0:
            if is_textured(mesh):
                failures.append(finding("%s has textures but no UV map." % obj.name, obj.name, plan_id(obj)))
            continue
        areas = polygon_areas(mesh)
        if areas is not None:
            empty = int(np.count_nonzero((areas[0] < EMPTY_UV_AREA) & (areas[1] > MIN_FACE_AREA)))
            if empty:
                warnings.append(finding("%s has %d face%s whose UVs have no area." % (
                    obj.name, empty, "" if empty == 1 else "s"), obj.name, plan_id(obj)))
        if obj.get("plynth_kind") == "item":
            triangles = uv_triangles(mesh)
            if triangles is not None and has_overlap(triangles):
                warnings.append(finding("%s has UVs that overlap, so a baked map would share space." % obj.name,
                                        obj.name, plan_id(obj)))
    if failures:
        return result("fail", "%d textured mesh%s no UVs." % (len(failures), " has" if len(failures) == 1 else "es have"),
                      failures + warnings)
    if warnings:
        return result("warning", "Some UVs are empty or overlap.", warnings)
    return result("pass", "Every textured mesh has usable UVs.")


QUALITY_CHECKS.register("uvs", QualityCheck(ORDER, "scene", "UVs", run))
