import os
import unittest

import bpy
import numpy as np

import fab_helpers
from fab_helpers import FabTest
from plynth import shading
from plynth.materials import read_pixels, write_png
from plynth.stores import fab_materials

UNLINKED = ["Wood_BaseColor.png", "Wood_Normal.png", "Wood_Roughness.png",
            "Fabric_BaseColor.png", "Fabric_Normal.png", "Fabric_Roughness.png"]


class Context:
    # What fab_materials needs of a job: a work folder and a place for warnings.
    def __init__(self, work):
        self.work_folder = work
        self.warnings = []

    # Keeps a warning.
    def warn(self, code, message):
        self.warnings.append((code, message))


class PickedNormalTests(FabTest):
    # Cleans the unlinked chair with picks.
    def clean_with(self, maps, normal):
        zip_file = fab_helpers.zip_path("chair_unlinked.zip")
        source = self.write_source(zip_file, "Chair.fbx", ["Chair.fbx"] + UNLINKED, UNLINKED, maps=maps, normal=normal)
        result = self.cleanup_fab(source)
        self.assertTrue(result["ok"], result["error"])
        return result

    # The map written for a material holds the source with its green flipped, or unchanged.
    def assert_normal(self, key, entry, flipped):
        source = read_pixels(self.path("work", "unpacked", entry))
        written = read_pixels(self.map_path(key, "normal"))
        green = 1.0 - source[:, :, 1] if flipped else source[:, :, 1]
        self.assertTrue(np.allclose(written[:, :, 1], green, atol=1.5 / 255), "green is wrong for %s" % key)
        self.assertTrue(np.allclose(written[:, :, 0], source[:, :, 0], atol=1.5 / 255), "red changed for %s" % key)

    # A normal map picked for one material follows the DirectX option.
    def test_named_pick_is_flipped(self):
        self.clean_with({"Wood": {"normal": "Wood_Normal.png"}}, "directx")
        self.assert_normal("wood", "Wood_Normal.png", True)
        self.assert_normal("fabric", "Fabric_Normal.png", True)

    # A normal map picked for every material follows it too.
    def test_pick_for_all_is_flipped(self):
        self.clean_with({"*": {"normal": "Wood_Normal.png"}}, "directx")
        self.assert_normal("wood", "Wood_Normal.png", True)
        self.assert_normal("fabric", "Wood_Normal.png", True)

    # A picked map with no convention in its name stays as it is by default.
    def test_auto_leaves_a_plain_pick(self):
        self.clean_with({"Wood": {"normal": "Wood_Normal.png"}}, "auto")
        self.assert_normal("wood", "Wood_Normal.png", False)


class PickTests(FabTest):
    # Makes two materials on two objects and their color files. With linked, each already reads its own file.
    def make_model(self, linked):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        root = self.path("pack")
        os.makedirs(root)
        materials = {}
        for name, value in (("Wood", 60), ("Metal", 180)):
            path = os.path.join(root, name + "_BaseColor.png")
            write_png(path, np.full((4, 4, 3), value, dtype=np.uint8))
            material = bpy.data.materials.new(name)
            if linked:
                shading.apply_maps(material, {"base_color": path})
            mesh = bpy.data.meshes.new(name)
            mesh.materials.append(material)
            bpy.context.scene.collection.objects.link(bpy.data.objects.new(name, mesh))
            materials[name] = material
        write_png(os.path.join(root, "Other_DX_Normal.png"), np.full((4, 4, 3), 60, dtype=np.uint8))
        return root, materials

    # Runs the texture matching with picks. Returns the warnings.
    def match(self, root, maps):
        context = Context(self.path("work"))
        textures = ["Wood_BaseColor.png", "Metal_BaseColor.png", "Other_DX_Normal.png"]
        fab_materials.apply(context, {"textures": textures, "maps": maps, "normal": "auto"}, root)
        return context.warnings

    # The file names of the images a material reads.
    def images_of(self, material):
        if material.node_tree is None:
            return set()
        return {os.path.basename(bpy.path.abspath(node.image.filepath)) for node in material.node_tree.nodes
                if node.type == "TEX_IMAGE" and node.image is not None}

    # A pick for Wood changes Wood only, also when both materials bring their own images.
    def test_named_pick_changes_one_material(self):
        for linked in (False, True):
            with self.subTest(linked=linked):
                self.folder = os.path.join(self.folder, "linked" if linked else "plain")
                os.makedirs(self.folder)
                root, materials = self.make_model(linked)
                self.match(root, {"Wood": {"base_color": "Metal_BaseColor.png"}})
                self.assertIn("Metal_BaseColor.png", self.images_of(materials["Wood"]))
                self.assertNotIn("Wood_BaseColor.png", self.images_of(materials["Wood"]))
                self.assertEqual(self.images_of(materials["Metal"]), {"Metal_BaseColor.png"})

    # A material's own pick wins over the pick for every material.
    def test_own_pick_wins(self):
        root, materials = self.make_model(False)
        self.match(root, {"*": {"base_color": "Metal_BaseColor.png"}, "Wood": {"base_color": "Wood_BaseColor.png"}})
        self.assertEqual(self.images_of(materials["Wood"]), {"Wood_BaseColor.png"})
        self.assertEqual(self.images_of(materials["Metal"]), {"Metal_BaseColor.png"})

    # A name the model does not have warns with that name and changes nothing.
    def test_unknown_material_warns(self):
        root, materials = self.make_model(True)
        warnings = self.match(root, {"Wodo": {"base_color": "Metal_BaseColor.png"}})
        named = [code for code, message in warnings if "Wodo" in message]
        self.assertEqual(named, ["missing_texture"])
        self.assertEqual(self.images_of(materials["Wood"]), {"Wood_BaseColor.png"})

    # A picked map whose name says DirectX is flipped by default.
    def test_auto_reads_the_picked_name(self):
        root, materials = self.make_model(False)
        self.match(root, {"Wood": {"normal": "Other_DX_Normal.png"}})
        greens = [float(read_pixels(bpy.path.abspath(node.image.filepath))[0, 0, 1]) * 255
                  for node in materials["Wood"].node_tree.nodes
                  if node.type == "TEX_IMAGE" and node.image is not None]
        self.assertTrue(any(abs(value - 195) <= 2 for value in greens), greens)


if __name__ == "__main__":
    unittest.main()
