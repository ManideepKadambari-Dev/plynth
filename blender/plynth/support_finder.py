"""Finds the flat places on a model where set dressing may put props, from its triangles. Plain Python, so it is
tested without Blender. The cleanup step measure_supports hands it the cleaned model's triangles.
"""
import bisect

MIN_SIDE = 0.1          # a support is at least this long on each side
MIN_CLEARANCE = 0.1     # and has at least this much free above it
OPEN_CLEARANCE = 2.0    # the clearance given when nothing of the model is above a support
MOST = 16               # supports kept, the largest first
UP = 0.999              # a face counts as facing up when its normal's z is at least this
SAME_LEVEL = 1e-4       # faces this close in height lie on one level


# Works out the supports from world triangles: upward faces grouped into rectangles per level, each with the
# free height above it, as dictionaries in the shape of asset-supports.schema.json. Only the largest MOST places
# that pass get their clearance measured, so a dense model stays quick.
def find_supports(triangles):
    rects = []
    for tri in triangles:
        normal = _normal(tri)
        if normal is None or normal[2] < UP:
            continue
        z = sum(p[2] for p in tri) / 3.0
        rects.append([min(p[0] for p in tri), min(p[1] for p in tri), max(p[0] for p in tri), max(p[1] for p in tri), z])
    merged = [r for r in _merge(rects) if r[2] - r[0] >= MIN_SIDE - 1e-9 and r[3] - r[1] >= MIN_SIDE - 1e-9]
    merged.sort(key=lambda r: -(r[2] - r[0]) * (r[3] - r[1]))
    above = _bounds(triangles)
    lows = [entry[0] for entry in above]
    out = []
    for x0, y0, x1, y1, z in merged:
        clearance = _clearance(above, lows, x0, y0, x1, y1, z)
        if clearance < MIN_CLEARANCE - 1e-9:
            continue
        out.append({"x0": x0, "y0": y0, "x1": x1, "y1": y1, "z": max(0.0, z), "clearance": clearance})
        if len(out) == MOST:
            break
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
    out = []
    for level in _levels(rects):
        out.extend(_merge_level(level))
    return [tuple(r) for r in out]


# Splits rectangles into groups by height, a new group starting where the next one is more than SAME_LEVEL higher.
def _levels(rects):
    groups = []
    current = []
    for rect in sorted(rects, key=lambda r: r[4]):
        if current and rect[4] - current[-1][4] > SAME_LEVEL:
            groups.append(current)
            current = []
        current.append(list(rect))
    if current:
        groups.append(current)
    return groups


# Joins the touching rectangles of one level. Each pass sweeps them from left to right and compares a rectangle
# only with the ones that still reach it; passes repeat until one joins nothing.
def _merge_level(rects):
    while True:
        rects.sort(key=lambda r: r[0])
        out = []
        active = []
        joined = False
        for rect in rects:
            active = [other for other in active if other[2] >= rect[0] - 1e-6]
            for other in active:
                if other[1] > rect[3] + 1e-6 or rect[1] > other[3] + 1e-6 or abs(other[4] - rect[4]) > SAME_LEVEL:
                    continue
                other[1] = min(other[1], rect[1])
                other[2] = max(other[2], rect[2])
                other[3] = max(other[3], rect[3])
                other[4] = max(other[4], rect[4])
                joined = True
                break
            else:
                out.append(rect)
                active.append(rect)
        rects = out
        if not joined:
            return rects


# Returns (lowest z, min x, max x, min y, max y) of every triangle, sorted by lowest z.
def _bounds(triangles):
    entries = []
    for tri in triangles:
        xs = [p[0] for p in tri]
        ys = [p[1] for p in tri]
        entries.append((min(p[2] for p in tri), min(xs), max(xs), min(ys), max(ys)))
    entries.sort(key=lambda entry: entry[0])
    return entries


# The free height above a rectangle: up to the lowest part of the model above it, or OPEN_CLEARANCE. The
# triangles are sorted by their lowest z, so the first one above the level that overlaps the rectangle is it.
def _clearance(above, lows, x0, y0, x1, y1, z):
    inset = 1e-4
    for index in range(bisect.bisect_right(lows, z + SAME_LEVEL), len(above)):
        low, min_x, max_x, min_y, max_y = above[index]
        if low - z >= OPEN_CLEARANCE:
            break
        if max_x <= x0 + inset or min_x >= x1 - inset or max_y <= y0 + inset or min_y >= y1 - inset:
            continue
        return low - z
    return OPEN_CLEARANCE
