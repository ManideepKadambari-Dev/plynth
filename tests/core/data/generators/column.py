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

# Test file. Core reads the declaration above and never runs this.
def generate(gen):
    return []
