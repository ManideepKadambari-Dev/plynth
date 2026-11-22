import json
import math
import os

from build_helpers import DATA_FOLDER

FRAME = 0.05
MARGIN = 0.05
THICKNESS = 0.2
OUTER = [(-0.1, 0.1), (-0.1, -4.1), (4.1, -4.1), (4.1, 0.1)]
HOLE = [(0.1, -0.1), (3.9, -0.1), (3.9, -3.9), (0.1, -3.9)]
ROOM = [(0.1, -0.1), (0.1, -3.9), (3.9, -3.9), (3.9, -0.1)]
SETTINGS = {"bevel_width": 0.01, "bevel_segments": 2, "fill_openings": True}


# Returns the points of an arc about (cx, cy) from angle a0 to a1 in radians, with count segments, ends included.
def arc(cx, cy, radius, a0, a1, count):
    return [(cx + radius * math.cos(a0 + (a1 - a0) * i / count), cy + radius * math.sin(a0 + (a1 - a0) * i / count))
            for i in range(count + 1)]


# The rectangle outline of a box from 0 to height.
def rectangle(width, height):
    return [(-width / 2, 0.0), (width / 2, 0.0), (width / 2, height), (-width / 2, height)]


# The round arch outline: straight sides, then a half circle.
def round_arch(width, height):
    r = width / 2
    return [(-r, 0.0), (r, 0.0)] + arc(0.0, height - r, r, 0.0, math.pi, 24)[:-1] + [(-r, height - r)]


# The pointed arch outline: two arcs of radius width meeting at the top.
def pointed_arch(width, height):
    r = width
    rise = width * math.sqrt(3) / 2
    base = height - rise
    right = arc(-width / 2, base, r, 0.0, math.pi / 3, 16)
    left = arc(width / 2, base, r, 2 * math.pi / 3, math.pi, 16)
    return [(-width / 2, 0.0), (width / 2, 0.0)] + right[:-1] + left[:-1] + [(-width / 2, base)]


# The ellipse that touches the four sides of the box.
def ellipse(width, height):
    return [(width / 2 * math.cos(2 * math.pi * i / 48), height / 2 + height / 2 * math.sin(2 * math.pi * i / 48))
            for i in range(48)]


# A keyhole: a round head on a narrow slot, a custom outline.
def keyhole(width, height):
    r = width / 2
    s = width * 0.2
    centre = height - r
    a = math.acos(s / r)
    head = arc(0.0, centre, r, -a, math.pi + a, 28)
    return [(-s, 0.0), (s, 0.0)] + head + []


# Moves each edge of a polygon inward by distance, keeping the edges listed as fixed where they are.
def inset(points, distance, fixed_bottom):
    count = len(points)
    result = []
    for i in range(count):
        p0, p1, p2 = points[i - 1], points[i], points[(i + 1) % count]

        def offset_line(a, b):
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = math.hypot(dx, dy)
            fixed = fixed_bottom and abs(a[1]) < 1e-9 and abs(b[1]) < 1e-9
            amount = 0.0 if fixed else distance
            nx, ny = -dy / length, dx / length
            return (a[0] + nx * amount, a[1] + ny * amount), (dx / length, dy / length), (nx, ny), amount

        a, da, na, ma = offset_line(p0, p1)
        b, db, nb, mb = offset_line(p1, p2)
        cross = da[0] * db[1] - da[1] * db[0]
        if abs(cross) < 0.05:
            result.append((p1[0] + na[0] * ma, p1[1] + na[1] * ma))
            continue
        t = ((b[0] - a[0]) * db[1] - (b[1] - a[1]) * db[0]) / cross
        result.append((a[0] + da[0] * t, a[1] + da[1] * t))
    return result


# Splits the edges of a (u, v) outline so no edge is longer than step in u.
def subdivide(points, step):
    result = []
    for i, p in enumerate(points):
        q = points[(i + 1) % len(points)]
        pieces = max(1, int(math.ceil(abs(q[0] - p[0]) / step)))
        for k in range(pieces):
            result.append((p[0] + (q[0] - p[0]) * k / pieces, p[1] + (q[1] - p[1]) * k / pieces))
    return result


# Works out one opening on a straight wall the way core does: the box outline, the inner outline, the cutter
# rings, the threshold. centre, tangent and right are world X and Y. wall_height says when the top touches the
# wall top.
def straight_opening(oid, wall, kind, shape, outline, width, height, sill, centre, tangent, right, elevation=0.0,
                     wall_height=3.0, hinge="start"):
    flat_bottom = sill == 0 and kind in ("door", "plain")
    inner = inset(outline, FRAME, flat_bottom)
    if flat_bottom:
        inner = [(u, max(v, 0.0)) for u, v in inner]
    half = THICKNESS / 2 + MARGIN
    rings = {}
    for name, w in (("right", half), ("left", -half)):
        ring = []
        for u, v in outline:
            z = elevation + sill + v
            if sill == 0 and v <= 1e-9:
                z -= MARGIN
            if sill + height >= wall_height - 1e-9 and v >= height - 1e-9:
                z += MARGIN
            ring.append([centre[0] + tangent[0] * u + right[0] * w, centre[1] + tangent[1] * u + right[1] * w, z])
        rings[name] = ring
    threshold = []
    if sill == 0:
        hw, ht = width / 2, THICKNESS / 2
        corners = [(-hw, -ht), (hw, -ht), (hw, ht), (-hw, ht)]
        threshold = [[centre[0] + tangent[0] * u + right[0] * w, centre[1] + tangent[1] * u + right[1] * w]
                     for u, w in corners]
        threshold.reverse()
    return opening_dict(oid, wall, kind, shape, width, height, sill, hinge, centre, tangent, right, False,
                        outline, inner, rings, threshold)


# Works out one opening on a curved wall: a circular arc about (cx, cy) of radius R, the opening at angle phi.
def curved_opening(oid, wall, kind, shape, outline, width, height, sill, cx, cy, radius, phi, hinge="start"):
    centre = (cx + radius * math.cos(phi), cy + radius * math.sin(phi))
    tangent = (-math.sin(phi), math.cos(phi))
    right = (math.cos(phi), math.sin(phi))
    flat_bottom = sill == 0 and kind in ("door", "plain")
    inner = inset(outline, FRAME, flat_bottom)
    if flat_bottom:
        inner = [(u, max(v, 0.0)) for u, v in inner]
    half = THICKNESS / 2 + MARGIN
    fine = subdivide(outline, 0.05)
    rings = {}
    for name, w in (("right", half), ("left", -half)):
        ring = []
        for u, v in fine:
            angle = phi + u / radius
            z = sill + v
            if sill == 0 and v <= 1e-9:
                z -= MARGIN
            if sill + height >= 3.0 - 1e-9 and v >= height - 1e-9:
                z += MARGIN
            ring.append([cx + (radius + w) * math.cos(angle), cy + (radius + w) * math.sin(angle), z])
        rings[name] = ring
    return opening_dict(oid, wall, kind, shape, width, height, sill, hinge, centre, tangent, right, True,
                        outline, inner, rings, [])


# Packs the fields of an opening of the job schema.
def opening_dict(oid, wall, kind, shape, width, height, sill, hinge, centre, tangent, right, curved, outline, inner,
                 rings, threshold):
    return {"id": oid, "wall": wall, "type": kind, "shape": shape, "width": width, "height": height, "sill": sill,
            "hinge": hinge, "swing": "left", "frame": None, "centre": list(centre), "tangent": list(tangent),
            "right": list(right), "thickness": THICKNESS, "curved": curved,
            "outline": [list(p) for p in outline], "inner_outline": [list(p) for p in inner],
            "cutter": rings, "threshold": threshold,
            "materials": {"reveal": None, "threshold": None, "door": None, "frame": None, "glass": None,
                          "handle": None}}


# Returns a wall ring dictionary from points and one wall.
def ring(points, edges):
    return {"points": [list(p) for p in points], "edges": edges}


# The floor of the square room with the given openings.
def square_floor(openings, elevation=0.0):
    outer = ring(OUTER, [{"wall": w, "side": "right", "room": None, "material": None} for w in ("w4", "w3", "w2", "w1")])
    hole = ring(HOLE, [{"wall": w, "side": "left", "room": "r1", "material": None} for w in ("w1", "w2", "w3", "w4")])
    room = {"id": "r1", "type": "living", "area": 14.44, "floor_z": elevation, "ceiling_z": elevation + 3.0,
            "floor_material": None, "ceiling_material": None, "polygons": [{"outline": [list(p) for p in ROOM], "holes": []}]}
    return {"id": "f1", "elevation": elevation, "ceiling_height": 3.0,
            "wall_solids": [{"height": 3.0, "top_material": None, "outline": outer, "holes": [hole]}],
            "openings": openings, "rooms": [room], "items": []}


# The floor of one free standing curved wall (a ring sector about the origin) with the given openings.
def curved_floor(openings, radius=3.0, a0=0.3, a1=2.4):
    count = 40
    outer = arc(0.0, 0.0, radius + THICKNESS / 2, a0, a1, count)
    inner = arc(0.0, 0.0, radius - THICKNESS / 2, a1, a0, count)
    points = outer + inner
    edges = [{"wall": "wc", "side": "right", "room": None, "material": None} for _ in range(count)]
    edges.append({"wall": "wc", "side": None, "room": None, "material": None})
    edges += [{"wall": "wc", "side": "left", "room": None, "material": None} for _ in range(count)]
    edges.append({"wall": "wc", "side": None, "room": None, "material": None})
    return {"id": "f1", "elevation": 0.0, "ceiling_height": 3.0,
            "wall_solids": [{"height": 3.0, "top_material": None, "outline": ring(points, edges), "holes": []}],
            "openings": openings, "rooms": [], "items": []}


# Wraps floors into a build job.
def job(floors, settings=None):
    return {"format": "plynth-blender-job", "version": 1, "task": "build", "job_id": "fixture-1",
            "result_file": "result.json",
            "params": {"project": {"name": "fixture", "folder": "project"}, "output_folder": "build",
                       "blend_file": "building.blend", "fbx_file": "building.fbx",
                       "materials_file": "materials.json", "textures_folder": "textures", "plan": {"version": 1},
                       "settings": settings or dict(SETTINGS), "assets": [], "materials": [], "floors": floors}}


TOP_WALL = {"centre": (2.0, 0.0), "tangent": (1.0, 0.0), "right": (0.0, -1.0)}


# Returns every opening fixture as a name and a job.
def fixtures():
    top = TOP_WALL
    return {
        "door": job([square_floor([straight_opening("o1", "w1", "door", "rectangle", rectangle(0.9, 2.1), 0.9, 2.1,
                                                    0.0, **top)])]),
        "round_arch_window": job([square_floor([straight_opening("o1", "w1", "window", "round_arch",
                                                                 round_arch(1.0, 1.5), 1.0, 1.5, 0.9, **top)])]),
        "pointed_arch_plain": job([square_floor([straight_opening("o1", "w1", "plain", "pointed_arch",
                                                                  pointed_arch(1.0, 2.4), 1.0, 2.4, 0.0, **top)])]),
        "circle_window": job([square_floor([straight_opening("o1", "w1", "window", "circle", ellipse(1.0, 1.0),
                                                             1.0, 1.0, 1.1, **top)])]),
        "keyhole": job([square_floor([straight_opening("o1", "w1", "plain", "keyhole", keyhole(1.0, 2.0), 1.0, 2.0,
                                                       0.0, **top)])]),
        "curved_door": job([curved_floor([curved_opening("o1", "wc", "door", "rectangle", rectangle(0.9, 2.1), 0.9,
                                                         2.1, 0.0, 0.0, 0.0, 3.0, 1.35)])]),
    }


# Writes the fixtures to tests/data/buildjob.
def write_fixtures():
    for name, data in fixtures().items():
        with open(os.path.join(DATA_FOLDER, name + ".json"), "w", encoding="utf-8") as handle:
            json.dump(data, handle, indent=1)


if __name__ == "__main__":
    write_fixtures()
