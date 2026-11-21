from .. import meshes

EPSILON = 1e-6


# Returns a function place(u, v, w) that gives the world point of a point in the opening box.
def placer(floor, opening):
    cx, cy = opening["centre"]
    tx, ty = opening["tangent"]
    rx, ry = opening["right"]
    z0 = floor["elevation"] + opening["sill"]

    def place(u, v, w):
        return (cx + tx * u + rx * w, cy + ty * u + ry * w, z0 + v)

    return place


# Returns the smallest and largest u of the polygon at height v.
def span_at(points, v):
    found = []
    for i, (u0, v0) in enumerate(points):
        u1, v1 = points[(i + 1) % len(points)]
        if (v0 - v) * (v1 - v) <= 0 and abs(v1 - v0) > EPSILON:
            found.append(u0 + (u1 - u0) * (v - v0) / (v1 - v0))
    if not found:
        return None
    return min(found), max(found)


# Returns the band between the outline and the inner outline as one polygon. When the inner outline stands on
# the bottom of the outline (a door) the band is open at the bottom, so it is one U shaped ring; otherwise the
# inner outline is a hole. Returns (ring, holes).
def frame_band(outline, inner):
    bottom = min(v for _, v in outline)
    touches = min(v for _, v in inner) <= bottom + EPSILON
    if not touches:
        return outline, [list(reversed(inner))]
    start = min(range(len(inner)), key=lambda i: (inner[i][1] > bottom + EPSILON, inner[i][0]))
    rotated = inner[start:] + inner[:start]
    bottom_right = 1
    while bottom_right + 1 < len(rotated) and rotated[bottom_right + 1][1] <= bottom + EPSILON:
        bottom_right += 1
    left_start = min(range(len(outline)), key=lambda i: (outline[i][1] > bottom + EPSILON, outline[i][0]))
    ring_outline = outline[left_start:] + outline[:left_start]
    right_end = 1
    while right_end + 1 < len(ring_outline) and ring_outline[right_end + 1][1] <= bottom + EPSILON:
        right_end += 1
    path = [rotated[0]] + list(reversed(rotated[bottom_right:]))
    return ring_outline[:1] + path + ring_outline[right_end:], []


# Adds an axis aligned box in the opening box, from (u0, v0, w0) to (u1, v1, w1).
def add_box(bm, place, u0, v0, w0, u1, v1, w1, slot):
    outline = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
    meshes.add_prism_uv(bm, outline, [], w0, w1, place, slot)


# Makes the object of one opening from a bmesh and the surface names of its slots.
def finish(ctx, floor, opening, bm, suffix, kind, surface_names):
    collection = ctx.state["collections"][floor["id"]]
    name = "SM_%s_%s_%s" % (floor["id"], opening["id"], suffix)
    obj = meshes.make_object(name, bm, collection, opening["id"], kind)
    meshes.add_slots(obj, surface_names)
    return obj
