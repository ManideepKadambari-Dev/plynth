"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Railing",
 "description": "A straight railing with posts, a top rail and a middle rail.",
 "addons": [],
 "size": {"width": {"param": "length"}, "depth": 0.05, "height": {"param": "height"}},
 "params": [
  {"id": "length", "label": "Length", "type": "number", "unit": "m", "default": 1.5, "min": 0.3, "max": 10},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": 0.9, "min": 0.3, "max": 2.0},
  {"id": "spacing", "label": "Post spacing", "type": "number", "unit": "m", "default": 0.9, "min": 0.2, "max": 3},
  {"id": "material", "label": "Material", "type": "material", "default": "brushed_steel"}]}
"""
import math

POST = 0.05


# Adds a closed box with the given x, y and z ranges to the vertex and face lists.
def add_box(verts, faces, x0, x1, y0, y1, z0, z1):
    start = len(verts)
    verts.extend([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                  (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)])
    for face in ([3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]):
        faces.append([start + i for i in face])


# Makes the railing around the origin: bottom centre at (0, 0, 0), running along X, front toward -Y.
def generate(gen):
    p = gen.params
    length = p["length"]
    height = p["height"]
    half = POST / 2.0
    count = max(2, int(math.ceil(length / p["spacing"])) + 1)
    first = -length / 2.0 + half
    step = (length - POST) / (count - 1)
    verts = []
    faces = []
    for i in range(count):
        x = first + step * i
        add_box(verts, faces, x - half, x + half, -half, half, 0.0, height - 0.04)
    add_box(verts, faces, -length / 2.0, length / 2.0, -half, half, height - 0.04, height)
    middle = height * 0.5
    add_box(verts, faces, -length / 2.0 + POST, length / 2.0 - POST, -0.01, 0.01, middle - 0.015, middle + 0.015)
    obj = gen.mesh_object("railing", verts, faces)
    obj.data.materials.append(gen.material("material"))
    return [obj]
