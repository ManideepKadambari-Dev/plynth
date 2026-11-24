"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Too Wide", "description": "Test generator.", "addons": [], "size": {"width": 1.0, "depth": 1.0, "height": 1.0}, "params": [{"id": "material", "label": "Material", "type": "material", "default": null}]}
"""


# Makes a box one metre wide, deep and high around the origin.
def box(gen, width=1.0):
    h = width / 2.0
    verts = [(-h, -0.5, 0), (h, -0.5, 0), (h, 0.5, 0), (-h, 0.5, 0), (-h, -0.5, 1), (h, -0.5, 1), (h, 0.5, 1), (-h, 0.5, 1)]
    faces = [[3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]
    return gen.mesh_object("box", verts, faces)


def generate(gen):
    return [box(gen, 1.05)]
