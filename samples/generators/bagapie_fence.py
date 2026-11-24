"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Post and Rail Fence",
 "description": "A straight fence with posts and rails, made with the Bagapie add-on.",
 "addons": ["bagapie"],
 "size": {"width": {"param": "length"}, "depth": 0.15, "height": {"param": "height"}},
 "params": [
  {"id": "length", "label": "Length", "type": "number", "unit": "m", "default": 3, "min": 0.5, "max": 20},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": 1.0, "min": 0.4, "max": 3},
  {"id": "post_spacing", "label": "Post spacing", "type": "number", "unit": "m", "default": 1.0, "min": 0.3, "max": 5},
  {"id": "material", "label": "Material", "type": "material", "default": "brushed_steel"}]}
"""


# Makes the fence along X with Bagapie. The end posts are 26 mm thick and stick out past the path, so the path
# is shorter than the item by that much.
def generate(gen):
    p = gen.params
    half = p["length"] / 2 - 0.013
    api = gen.bridge("bagapie").api
    made = api.fence([(-half, 0.0, 0.0), (half, 0.0, 0.0)], height=p["height"],
                     post_spacing=p["post_spacing"])
    for obj in made:
        gen.link(obj)
        obj.data.materials.clear()
        obj.data.materials.append(gen.material("material"))
    return made
