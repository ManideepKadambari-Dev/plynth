"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Column",
 "description": "A round or square column from the floor up.",
 "addons": [],
 "size": {"width": {"param": "diameter"}, "depth": {"param": "diameter"}, "height": {"param": "height"}},
 "params": [
  {"id": "diameter", "label": "Diameter", "type": "number", "unit": "m", "default": 0.3, "min": 0.05, "max": 2.0},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": "ceiling_height", "min": 0.1, "max": 20},
  {"id": "shape", "label": "Shape", "type": "choice", "default": "round",
   "choices": [{"id": "round", "label": "Round"}, {"id": "square", "label": "Square"}]},
  {"id": "sides", "label": "Sides", "type": "integer", "default": 24, "min": 3, "max": 64},
  {"id": "plinth", "label": "Plinth and cap", "type": "boolean", "default": true},
  {"id": "material", "label": "Material", "type": "material", "default": "white_paint"}]}
"""
import math


# Returns the corners of the column cross section, anticlockwise seen from above.
def ring(shape, sides, half):
    if shape == "square":
        return [(half, -half), (half, half), (-half, half), (-half, -half)]
    return [(half * math.cos(2 * math.pi * i / sides), half * math.sin(2 * math.pi * i / sides))
            for i in range(sides)]


# Adds a closed prism over the ring between z0 and z1 to the vertex and face lists.
def add_prism(verts, faces, points, z0, z1):
    start = len(verts)
    total = len(points)
    verts.extend((x, y, z0) for x, y in points)
    verts.extend((x, y, z1) for x, y in points)
    faces.append(list(range(start + total - 1, start - 1, -1)))
    faces.append(list(range(start + total, start + 2 * total)))
    for i in range(total):
        j = (i + 1) % total
        faces.append([start + i, start + j, start + total + j, start + total + i])


# Makes the column around the origin: bottom centre at (0, 0, 0), front toward -Y.
def generate(gen):
    p = gen.params
    half = p["diameter"] / 2.0
    height = p["height"]
    verts = []
    faces = []
    if p["plinth"]:
        block = min(0.1, height / 4.0)
        add_prism(verts, faces, ring(p["shape"], p["sides"], half), 0.0, block)
        add_prism(verts, faces, ring(p["shape"], p["sides"], half * 0.85), block, height - block)
        add_prism(verts, faces, ring(p["shape"], p["sides"], half), height - block, height)
    else:
        add_prism(verts, faces, ring(p["shape"], p["sides"], half), 0.0, height)
    obj = gen.mesh_object("column", verts, faces)
    obj.data.materials.append(gen.material("material"))
    return [obj]
