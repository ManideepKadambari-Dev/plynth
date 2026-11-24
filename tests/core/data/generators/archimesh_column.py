"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Archimesh column",
 "description": "A column made with the Archimesh add-on.",
 "addons": ["archimesh"],
 "size": {"width": {"param": "width"}, "depth": {"param": "width", "times": 1, "plus": 0}, "height": {"param": "height"}},
 "params": [
  {"id": "width", "label": "Width", "type": "number", "unit": "m", "default": 0.4, "min": 0.1, "max": 2.0},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": "ceiling_height", "min": 0.5, "max": 10},
  {"id": "material", "label": "Material", "type": "material", "default": null}]}
"""

# Test file. Core reads the declaration above and never runs this.
def generate(gen):
    return gen.bridge("archimesh").column(gen.size)
