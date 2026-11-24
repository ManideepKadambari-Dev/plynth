"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Slotted Screen",
 "description": "A freestanding screen panel with vertical slots cut through it, made with the Bool Tool add-on.",
 "addons": ["bool_tool"],
 "size": {"width": {"param": "width"}, "depth": {"param": "thickness"}, "height": {"param": "height"}},
 "params": [
  {"id": "width", "label": "Width", "type": "number", "unit": "m", "default": 1.2, "min": 0.4, "max": 4},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": 2.0, "min": 0.5, "max": 4},
  {"id": "thickness", "label": "Thickness", "type": "number", "unit": "m", "default": 0.05, "min": 0.02, "max": 0.3},
  {"id": "slots", "label": "Slots", "type": "integer", "default": 5, "min": 1, "max": 12},
  {"id": "material", "label": "Material", "type": "material", "default": "white_paint"}]}
"""


# Returns the vertices and faces of a box with the given x, y and z ranges.
def box(x0, x1, y0, y1, z0, z1):
    verts = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    faces = [[3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]
    return verts, faces


# Cuts evenly spaced slots through a panel with Bool Tool. The panel is centred on x, y with its bottom at z 0.
def generate(gen):
    p = gen.params
    width, height, thick, count = p["width"], p["height"], p["thickness"], p["slots"]
    api = gen.bridge("bool_tool").api
    panel = gen.mesh_object("screen", *box(-width / 2, width / 2, -thick / 2, thick / 2, 0.0, height))
    cell = width / (count + 1)
    slot = min(0.04, cell / 2.0)
    cutters = []
    for i in range(count):
        x = -width / 2 + cell * (i + 1)
        cutters.append(gen.mesh_object("slot", *box(x - slot / 2, x + slot / 2, -thick, thick,
                                                    height * 0.2, height * 0.85)))
    made = api.difference(panel, cutters)
    for obj in made:
        obj.data.materials.clear()
        obj.data.materials.append(gen.material("material"))
    return made
