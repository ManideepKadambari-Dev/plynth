"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Archimesh Column",
 "description": "A round or square column with a base and a cap, made with the Archimesh add-on.",
 "addons": ["archimesh"],
 "size": {"width": {"param": "width"}, "depth": {"param": "width"}, "height": {"param": "height"}},
 "params": [
  {"id": "width", "label": "Width", "type": "number", "unit": "m", "default": 0.4, "min": 0.1, "max": 2.0},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": "ceiling_height", "min": 0.3, "max": 20},
  {"id": "shape", "label": "Shape", "type": "choice", "default": "round",
   "choices": [{"id": "round", "label": "Round"}, {"id": "square", "label": "Square"}]},
  {"id": "material", "label": "Material", "type": "material", "default": "white_paint"}]}
"""


# Makes the column with Archimesh. The base and cap plates are as wide as the item, and the shaft is narrower.
def generate(gen):
    p = gen.params
    api = gen.bridge("archimesh").api
    made = api.column(height=p["height"], width=p["width"] / 1.3, shape=p["shape"], plates=True,
                      plate_width=p["width"])
    for obj in made:
        gen.link(obj)
        obj.data.materials.clear()
        obj.data.materials.append(gen.material("material"))
    return made
