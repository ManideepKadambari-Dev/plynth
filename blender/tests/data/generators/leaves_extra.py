"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Leaves Extra", "description": "Test generator.", "addons": [], "size": {"width": 1.0, "depth": 1.0, "height": 1.0}, "params": [{"id": "material", "label": "Material", "type": "material", "default": null}]}
"""


# Makes a box one metre wide, deep and high around the origin.
def box(gen, width=1.0):
    h = width / 2.0
    verts = [(-h, -0.5, 0), (h, -0.5, 0), (h, 0.5, 0), (-h, 0.5, 0), (-h, -0.5, 1), (h, -0.5, 1), (h, 0.5, 1), (-h, 0.5, 1)]
    faces = [[3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]
    return gen.mesh_object("box", verts, faces)

import bpy


def generate(gen):
    main = box(gen)
    extra = box(gen)
    extra.name = "leftover_object"
    extra.data.name = "leftover_mesh"
    gen.link(bpy.data.objects.new("leftover_empty", None))
    bpy.data.meshes.new("unused_mesh")
    bpy.data.materials.new("unused_material")
    bpy.data.node_groups.new("unused_nodes", "GeometryNodeTree")
    bpy.data.collections.new("unused_collection")
    return [main]
