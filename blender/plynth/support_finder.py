"""Finds the flat places on a model where set dressing may put props, from its triangles. Plain Python, so it is
tested without Blender. The cleanup step measure_supports hands it the cleaned model's triangles.
"""

MIN_SIDE = 0.1          # a support is at least this long on each side
MIN_CLEARANCE = 0.1     # and has at least this much free above it
OPEN_CLEARANCE = 2.0    # the clearance given when nothing of the model is above a support
MOST = 16               # supports kept, the largest first
UP = 0.999              # a face counts as facing up when its normal's z is at least this
SAME_LEVEL = 1e-4       # faces this close in height lie on one level


# Works out the supports from world triangles: upward faces grouped into rectangles per level, each with the
# free height above it, as dictionaries in the shape of asset-supports.schema.json.
def find_supports(triangles):
    rects = []
    for tri in triangles:
        normal = _normal(tri)
        if normal is None or normal[2] < UP:
            continue
        z = sum(p[2] for p in tri) / 3.0
        rects.append([min(p[0] for p in tri), min(p[1] for p in tri), max(p[0] for p in tri), max(p[1] for p in tri), z])
    merged = _merge(rects)
    out = []
    for x0, y0, x1, y1, z in merged:
        if x1 - x0 < MIN_SIDE - 1e-9 or y1 - y0 < MIN_SIDE - 1e-9:
            continue
        clearance = _clearance(triangles, x0, y0, x1, y1, z)
        if clearance < MIN_CLEARANCE - 1e-9:
            continue
        out.append({"x0": x0, "y0": y0, "x1": x1, "y1": y1, "z": max(0.0, z), "clearance": clearance})
    out.sort(key=lambda s: -(s["x1"] - s["x0"]) * (s["y1"] - s["y0"]))
    out = out[:MOST]
    out.sort(key=lambda s: (s["z"], s["x0"], s["y0"]))
    return out


# The unit normal of a triangle, or None when it has no area.
def _normal(tri):
    a, b, c = tri
    u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    size = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5
    if size < 1e-12:
        return None
    return (n[0] / size, n[1] / size, n[2] / size)


# Joins rectangles on one level that touch or overlap into their bounding rectangle, until none touch.
def _merge(rects):
    rects = [list(r) for r in rects]
    changed = True
    while changed:
        changed = False
        for i in range(len(rects)):
            for j in range(i + 1, len(rects)):
                a, b = rects[i], rects[j]
                if abs(a[4] - b[4]) > SAME_LEVEL:
                    continue
                if a[0] > b[2] + 1e-6 or b[0] > a[2] + 1e-6 or a[1] > b[3] + 1e-6 or b[1] > a[3] + 1e-6:
                    continue
                rects[i] = [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]), max(a[4], b[4])]
                del rects[j]
                changed = True
                break
            if changed:
                break
    return [tuple(r) for r in rects]


# The free height above a rectangle: up to the lowest part of the model above it, or OPEN_CLEARANCE.
def _clearance(triangles, x0, y0, x1, y1, z):
    inset = 1e-4
    best = OPEN_CLEARANCE
    for tri in triangles:
        low = min(p[2] for p in tri)
        if low <= z + SAME_LEVEL:
            continue
        if max(p[0] for p in tri) <= x0 + inset or min(p[0] for p in tri) >= x1 - inset:
            continue
        if max(p[1] for p in tri) <= y0 + inset or min(p[1] for p in tri) >= y1 - inset:
            continue
        best = min(best, low - z)
    return best
