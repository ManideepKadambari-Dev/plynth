"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Square column",
 "description": "A square column from the floor up, with an optional base and capital.",
 "addons": [],
 "size": {"width": {"param": "width"}, "depth": {"param": "width"}, "height": {"param": "height"}},
 "params": [
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": "ceiling_height", "min": 0.3, "max": 20},
  {"id": "width", "label": "Width", "type": "number", "unit": "m", "default": 0.3, "min": 0.05, "max": 2.0,
   "help": "The widest part: the base and capital when they are on, otherwise the shaft."},
  {"id": "base", "label": "Base", "type": "boolean", "default": true},
  {"id": "base_height", "label": "Base height", "type": "number", "unit": "m", "default": 0.15, "min": 0.02, "max": 1.0},
  {"id": "capital", "label": "Capital", "type": "boolean", "default": true},
  {"id": "capital_height", "label": "Capital height", "type": "number", "unit": "m", "default": 0.15, "min": 0.02, "max": 1.0},
  {"id": "material", "label": "Material", "type": "material", "default": "white_paint",
   "help": "The catalog material of the whole column."}]}
"""
import math

SHAFT = 0.8
LOWER_TIER = 0.6
UVMAP = "UVMap"
SQUARE = [(1.0, -1.0), (1.0, 1.0), (-1.0, 1.0), (-1.0, -1.0)]


# Returns the base and capital heights in use, both made smaller when together they take over half the column.
def block_heights(p):
    base = p["base_height"] if p["base"] else 0.0
    capital = p["capital_height"] if p["capital"] else 0.0
    limit = p["height"] * 0.5
    if base + capital > limit:
        scale = limit / (base + capital)
        base *= scale
        capital *= scale
    return base, capital


# Returns the side outline from bottom to top as (half width, height) pairs: base, shaft and capital.
def profile(half, height, base, capital, taper):
    shaft = half * SHAFT if base > 0.0 or capital > 0.0 else half
    top = shaft * taper
    points = []
    if base > 0.0:
        step = (half + shaft) / 2.0
        points += [(half, 0.0), (half, base * LOWER_TIER), (step, base * LOWER_TIER), (step, base)]
    points += [(shaft, base), (top, height - capital)]
    if capital > 0.0:
        step = (half + top) / 2.0
        lower = height - capital * LOWER_TIER
        points += [(step, height - capital), (step, lower), (half, lower), (half, height)]
    kept = []
    for point in points:
        if not kept or abs(point[0] - kept[-1][0]) > 1e-7 or abs(point[1] - kept[-1][1]) > 1e-7:
            kept.append(point)
    return kept


# Sweeps the cross section up the outline into one closed mesh, with corner UVs in metres and smooth flags.
def loft(section, steps, smooth_sides):
    count = len(section)
    around = [0.0]
    for i in range(count):
        a, b = section[i], section[(i + 1) % count]
        around.append(around[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    along = [0.0]
    for k in range(1, len(steps)):
        along.append(along[-1] + math.hypot(steps[k][0] - steps[k - 1][0], steps[k][1] - steps[k - 1][1]))
    verts, faces, uvs, smooth = [], [], [], []
    for scale, z in steps:
        verts.extend((x * scale, y * scale, z) for x, y in section)
    for k in range(len(steps) - 1):
        low, high = k * count, (k + 1) * count
        s0, s1 = steps[k][0], steps[k + 1][0]
        v0, v1 = along[k], along[k + 1]
        upright = abs(steps[k + 1][1] - steps[k][1]) > 1e-7
        for i in range(count):
            j = (i + 1) % count
            faces.append([low + i, low + j, high + j, high + i])
            uvs.append([(around[i] * s0, v0), (around[i + 1] * s0, v0), (around[i + 1] * s1, v1),
                        (around[i] * s1, v1)])
            smooth.append(smooth_sides and upright)
    last = (len(steps) - 1) * count
    for ring in (list(range(count - 1, -1, -1)), list(range(last, last + count))):
        faces.append(ring)
        uvs.append([(verts[i][0], verts[i][1]) for i in ring])
        smooth.append(False)
    return verts, faces, uvs, smooth


# Gives the object's faces their corner UVs and smooth shading, in the order the faces were made.
def apply_faces(obj, uvs, smooth):
    mesh = obj.data
    layer = mesh.uv_layers.get(UVMAP) or mesh.uv_layers.new(name=UVMAP)
    for polygon, corners, flag in zip(mesh.polygons, uvs, smooth):
        polygon.use_smooth = flag
        for index, uv in zip(polygon.loop_indices, corners):
            layer.uv[index].vector = uv


# Makes the column around the origin: bottom centre at (0, 0, 0), front toward -Y.
def generate(gen):
    p = gen.params
    base, capital = block_heights(p)
    steps = profile(p["width"] / 2.0, p["height"], base, capital, 1.0)
    verts, faces, uvs, smooth = loft(SQUARE, steps, False)
    obj = gen.mesh_object("column", verts, faces)
    apply_faces(obj, uvs, smooth)
    gen.keep_uvs = True
    obj.data.materials.append(gen.material("material"))
    return [obj]
