import os
import unittest

import bpy
import numpy as np

import helpers
from plynth.materials import read_pixels, write_png
from plynth.stores import fab_materials

# Pairs with their own texture sets whose names become one file name on Windows today.
PAIRS = (("木", "石"), ("Wood+A", "Wood&A"), ("OakWood", "Oakwood"))
BASE = (250, 250, 250)
# The map each case starts from for the two materials, and a value the converted map must hold.
CASES = {"gloss": ("Gloss", ((51, 51, 51), (204, 204, 204)), (204, 51)),
         "packed": ("ORM", ((40, 90, 140), (200, 160, 110)), (90, 160)),
         "directx": ("Normal_DX", ((60, 60, 220), (100, 100, 220)), (195, 155))}


class Context:
    # What fab_materials needs of a job: a work folder and a place for warnings.
    def __init__(self, work):
        self.work_folder = work
        self.warnings = []

    # Keeps a warning.
    def warn(self, code, message):
        self.warnings.append((code, message))


class GeneratedMapTests(helpers.TempFolderTest):
    # Each material keeps the maps made from its own textures.
    def test_materials_do_not_share_generated_maps(self):
        for number, names in enumerate(PAIRS):
            for case in sorted(CASES):
                with self.subTest(names=names, case=case):
                    self.check_pair("%d_%s" % (number, case), names, CASES[case])

    # Builds two materials from their textures and checks the files that each one reads.
    def check_pair(self, label, names, case):
        word, colors, expected = case
        bpy.ops.wm.read_factory_settings(use_empty=True)
        root = self.path(label, "pack")
        os.makedirs(root)
        entries = []
        materials = []
        for index, name in enumerate(names):
            for channel, color in (("BaseColor", BASE), (word, colors[index])):
                # The folder keeps two names that differ only in case apart on disk.
                entry = "set%d/%s_%s.png" % (index, name, channel)
                os.makedirs(os.path.join(root, "set%d" % index), exist_ok=True)
                write_png(os.path.join(root, entry), np.full((4, 4, 3), color, dtype=np.uint8))
                entries.append(entry)
            material = bpy.data.materials.new(name)
            mesh = bpy.data.meshes.new("mesh%d" % index)
            mesh.materials.append(material)
            bpy.context.scene.collection.objects.link(bpy.data.objects.new("object%d" % index, mesh))
            materials.append(material)
        fab_materials.apply(Context(self.path(label, "work")), {"textures": entries, "maps": {}, "normal": "auto"}, root)

        files = []
        for index, material in enumerate(materials):
            paths = {os.path.normcase(os.path.abspath(bpy.path.abspath(node.image.filepath)))
                     for node in material.node_tree.nodes if node.type == "TEX_IMAGE" and node.image is not None}
            made = {path for path in paths if "fab_maps" in path}
            self.assertTrue(made, "no map was made for %s" % material.name)
            files.append(made)
            greens = [float(read_pixels(path)[0, 0, 1]) * 255 for path in made]
            self.assertTrue(any(abs(value - expected[index]) <= 2 for value in greens),
                            "%s reads %s, not its own %d" % (material.name, greens, expected[index]))
        self.assertFalse(files[0] & files[1], "two materials share a generated map")


if __name__ == "__main__":
    unittest.main()
