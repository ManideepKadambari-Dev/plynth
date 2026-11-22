"""Roof faces worked out by hand for the roof fixtures, by the rules of core/include/plynth/core/Roofs.h.

The section is 6 m by 4 m, x from 0 to 6 and world y from -4 to 0 (plan y from 0 to 4). The pitch is 30
degrees, the overhang 0.3 m, the layer 0.15 m thick measured straight down. Run this file with plain Python to
write the fixtures roof_flat.json, roof_gable.json and roof_two_parts.json into tests/data/buildjob.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from slot_fixtures import upgrade  # noqa: E402

DATA_FOLDER = os.path.join(HERE, "data", "buildjob")

X0, X1 = 0.0, 6.0
Y0, Y1 = -4.0, 0.0
THICKNESS = 0.15
PITCH = math.radians(30.0)
SLOPE = math.tan(PITCH)
OVERHANG = 0.3
PARAPET = 0.6
PARAPET_WALL = 0.2
CEILING = 3.0
SLAB = 0.2
WALL = 0.2
RIDGE_Y = (Y0 + Y1) / 2.0
HALF_SPAN = (Y1 - Y0) / 2.0


# Returns the normal of a ring of 3D points by Newell's method, not normalised.
def newell(points):
    pairs = list(zip(points, points[1:] + points[:1]))
    return (sum((p[1] - q[1]) * (p[2] + q[2]) for p, q in pairs),
            sum((p[2] - q[2]) * (p[0] + q[0]) for p, q in pairs),
            sum((p[0] - q[0]) * (p[1] + q[1]) for p, q in pairs))


# Returns the points ordered so that the ring runs anticlockwise seen from the side the direction points to.
def facing(points, direction):
    normal = newell(points)
    if sum(n * d for n, d in zip(normal, direction)) < 0:
        return list(reversed(points))
    return list(points)


# Makes a face of the job.
def face(part, outline, direction, holes=(), material=None):
    back = tuple(-v for v in direction)
    return {"part": part, "outline": [list(p) for p in facing(outline, direction)],
            "holes": [[list(p) for p in facing(hole, back)] for hole in holes],
            "material": material}


# Returns a vertical rectangle on the line a to b (x, y) between two heights, facing the way given.
def wall(part, a, b, z0, z1, direction, material=None):
    points = [(a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1)]
    return face(part, points, direction, material=material)


# Returns the four corners of a rectangle at one height.
def rectangle(x0, y0, x1, y1, z):
    return [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]


# The section's outline in world coordinates (world y is minus the plan y), with the points core keeps where the
# wall bands meet on a straight side.
SECTION = [(x, -y) for x, y in ((0.0, 0.0), (0.2, 0.0), (6.0, 0.0), (6.0, 4.0), (5.8, 4.0), (0.2, 4.0), (0.0, 4.0))]


# Returns the direction pointing away from the middle of the section, for the edge from a to b.
def outward_of(a, b):
    normal = (b[1] - a[1], a[0] - b[0], 0.0)
    middle = ((X0 + X1) / 2.0, (Y0 + Y1) / 2.0)
    away = (a[0] + b[0]) / 2.0 - middle[0], (a[1] + b[1]) / 2.0 - middle[1]
    return normal if normal[0] * away[0] + normal[1] * away[1] > 0 else tuple(-v for v in normal)


# The faces of the flat roof with a parapet, on the slab top at base. The covering spans the whole section and the
# parapet stands on its outline.
def flat_faces(base, parapet=PARAPET):
    layer = base + THICKNESS
    top = layer + parapet
    inset = PARAPET_WALL
    inner_corners = [(X0 + inset, Y0 + inset), (X1 - inset, Y0 + inset), (X1 - inset, Y1 - inset),
                     (X0 + inset, Y1 - inset)]
    ring = [(x, y, base) for x, y in SECTION]
    faces = [
        face("covering", [(x, y, layer) for x, y in SECTION], (0, 0, 1)),
        face("underside", ring, (0, 0, -1)),
        face("parapet", [(x, y, top) for x, y in SECTION], (0, 0, 1),
             holes=[rectangle(X0 + inset, Y0 + inset, X1 - inset, Y1 - inset, top)]),
    ]
    for a, b in zip(SECTION, SECTION[1:] + SECTION[:1]):
        outward = outward_of(a, b)
        faces.append(wall("edge", a, b, base, layer, outward))
        faces.append(wall("parapet", a, b, layer, top, outward))
    corners = [(X0 + inset, Y0 + inset), (X1 - inset, Y0 + inset), (X1 - inset, Y1 - inset), (X0 + inset, Y1 - inset)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        faces.append(wall("parapet", a, b, layer, top, tuple(-v for v in outward_of(a, b))))
    return faces


# Returns the height of the top surface of the gable roof at y: base at the section's edge, rising to the ridge.
def gable_top(base, y):
    return base + SLOPE * (HALF_SPAN - abs(y - RIDGE_Y))


# The faces of the gable roof with its ridge along x, on the slab top at base.
def gable_faces(base):
    ex0, ex1 = X0 - OVERHANG, X1 + OVERHANG
    ey0, ey1 = Y0 - OVERHANG, Y1 + OVERHANG
    faces = []
    for low, high, direction in ((RIDGE_Y, ey1, (0, SLOPE, 1)), (ey0, RIDGE_Y, (0, -SLOPE, 1))):
        top = [(ex0, low, gable_top(base, low)), (ex1, low, gable_top(base, low)),
               (ex1, high, gable_top(base, high)), (ex0, high, gable_top(base, high))]
        faces.append(face("covering", top, direction))
        under = [(x, y, z - THICKNESS) for x, y, z in top]
        faces.append(face("underside", under, (0, -direction[1], -1)))
    for y, direction in ((ey1, (0, 1, 0)), (ey0, (0, -1, 0))):
        top = gable_top(base, y)
        faces.append(wall("edge", (ex0, y), (ex1, y), top - THICKNESS, top, direction))
    for x, direction in ((ex0, (-1, 0, 0)), (ex1, (1, 0, 0))):
        for low, high in ((ey0, RIDGE_Y), (RIDGE_Y, ey1)):
            ring = [(x, low, gable_top(base, low)), (x, high, gable_top(base, high)),
                    (x, high, gable_top(base, high) - THICKNESS), (x, low, gable_top(base, low) - THICKNESS)]
            faces.append(face("edge", ring, direction))
    reach = HALF_SPAN - THICKNESS / SLOPE
    peak = gable_top(base, RIDGE_Y) - THICKNESS
    for x, direction in ((X0, (-1, 0, 0)), (X1, (1, 0, 0))):
        ring = [(x, RIDGE_Y - reach, base), (x, RIDGE_Y + reach, base), (x, RIDGE_Y, peak)]
        faces.append(face("gable", ring, direction))
    return faces


# Returns a ring of wall edges: the walls as the other fixtures name them.
def wall_ring(points, room, names):
    edges = [{"wall": name, "side": "left" if room else "right", "room": room, "material": None} for name in names]
    return {"points": points, "edges": edges}


# Returns the build floor of a 6 by 4 m room with walls, at an elevation, with its roofs.
def floor(floor_id, elevation, roofs):
    outer = [[X0, Y1], [X0, Y0], [X1, Y0], [X1, Y1]]
    inner = [[X0 + WALL, Y1 - WALL], [X1 - WALL, Y1 - WALL], [X1 - WALL, Y0 + WALL], [X0 + WALL, Y0 + WALL]]
    room_ring = [[X0 + WALL, Y1 - WALL], [X0 + WALL, Y0 + WALL], [X1 - WALL, Y0 + WALL], [X1 - WALL, Y1 - WALL]]
    polygon = {"outline": room_ring, "holes": []}
    return {
        "id": floor_id,
        "elevation": elevation,
        "ceiling_height": CEILING,
        "wall_solids": [{
            "height": CEILING,
            "top_material": None,
            "outline": wall_ring(outer, None, ["w4", "w3", "w2", "w1"]),
            "holes": [wall_ring(inner, "r1", ["w1", "w2", "w3", "w4"])],
        }],
        "openings": [],
        "rooms": [{
            "id": "r1", "type": "living", "area": (X1 - X0 - 2 * WALL) * (Y1 - Y0 - 2 * WALL),
            "floor_z": elevation, "ceiling_z": elevation + CEILING, "polygons": [polygon],
            "ceiling_polygons": [polygon], "floor_material": None, "ceiling_material": None,
        }],
        "items": [],
        "slab": {"bottom": elevation + CEILING, "top": elevation + CEILING + SLAB, "polygons": [], "undersides": [],
                 "sides": [], "tops": [], "materials": {"edge": None, "well": None, "underside": None}},
        "stairs": [],
        "roofs": roofs,
    }


# Returns the job of a building with the floors.
def job(floors):
    data = {
        "format": "plynth-blender-job", "version": 1, "task": "build", "job_id": "fixture-1",
        "result_file": "result.json",
        "params": {
            "project": {"name": "fixture", "folder": "project"},
            "output_folder": "build", "blend_file": "building.blend", "fbx_file": "building.fbx",
            "materials_file": "materials.json", "textures_folder": "textures", "preview_folder": "preview",
            "plan": {"version": 5},
            "settings": {"bevel_width": 0.01, "bevel_segments": 2, "fill_openings": True},
            "assets": [], "materials": [], "floors": floors,
        },
    }
    upgrade(data["params"])
    return data


# Returns the roof entry of a job floor.
def roof(type_name, faces, index=0, section=None):
    return {"index": index, "section": section, "type": type_name, "faces": faces}


# Returns the base of the roof of a floor at an elevation: the top of the slab above it.
def base_of(elevation):
    return elevation + CEILING + SLAB


# Returns every fixture as {file name: job}.
def fixtures():
    upper = SLAB + CEILING
    return {
        "roof_flat": job([floor("f1", 0.0, [roof("flat", flat_faces(base_of(0.0)))])]),
        "roof_gable": job([floor("f1", 0.0, [roof("gable", gable_faces(base_of(0.0)), section="rf1")])]),
        "roof_two_parts": job([floor("f1", 0.0, []),
                               floor("f2", upper, [roof("gable", gable_faces(base_of(upper)))])]),
    }


# Writes the fixture files.
def write_fixtures():
    for name, data in fixtures().items():
        with open(os.path.join(DATA_FOLDER, name + ".json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(data, handle, indent=1)
            handle.write("\n")


# Returns the area of a planar 3D ring.
def ring_area(points):
    x, y, z = newell(points)
    return math.sqrt(x * x + y * y + z * z) / 2.0


# Returns the area of a job face: its outline less its holes.
def face_area(source):
    return ring_area(source["outline"]) - sum(ring_area(hole) for hole in source["holes"])


# Returns the largest distance of the face's points from the plane through its outline.
def planarity(source):
    points = source["outline"]
    nx, ny, nz = newell(points)
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / length, ny / length, nz / length
    origin = points[0]
    return max(abs((p[0] - origin[0]) * nx + (p[1] - origin[1]) * ny + (p[2] - origin[2]) * nz)
               for ring in [points] + list(source["holes"]) for p in ring)


if __name__ == "__main__":
    write_fixtures()
