"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Base Cabinets",
 "description": "A run of base cabinets with doors, made with the Home Builder add-on.",
 "addons": ["home_builder_5"],
 "size": {"width": {"param": "length"}, "depth": {"param": "depth"}, "height": {"param": "height"}},
 "params": [
  {"id": "length", "label": "Length", "type": "number", "unit": "m", "default": 1.8, "min": 0.4, "max": 6},
  {"id": "depth", "label": "Depth", "type": "number", "unit": "m", "default": 0.6, "min": 0.3, "max": 1.0},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": 0.87, "min": 0.5, "max": 1.2},
  {"id": "material", "label": "Material", "type": "material", "default": "white_paint"}]}
"""


# Returns the x and y size of the first object.
def footprint(objects):
    points = [v.co for v in objects[0].data.vertices]
    return [max(c[a] for c in points) - min(c[a] for c in points) for a in (0, 1)]


# Makes cabinets about 60 cm wide that fill the length exactly. The doors stand 27 mm proud at the front and the
# back panel 27 mm behind the cabinet box, so the box is shallower than the item by 54 mm. The add-on now and then
# leaves a part at the wrong size, so a result that is off is made again, up to five times.
def generate(gen):
    p = gen.params
    api = gen.bridge("home_builder_5").api
    count = max(1, round(p["length"] / 0.6))
    for _ in range(5):
        made = api.base_cabinets(count=count, width=p["length"] / count, height=p["height"],
                                 depth=p["depth"] - 0.054)
        width, depth = footprint(made)
        if abs(width - p["length"]) < 0.005 and abs(depth - p["depth"]) < 0.005:
            break
    for obj in made:
        gen.link(obj)
        obj.data.materials.clear()
        obj.data.materials.append(gen.material("material"))
    return made
