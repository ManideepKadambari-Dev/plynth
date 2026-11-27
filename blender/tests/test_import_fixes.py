"""Model import cleanup: glass stays see-through, leaf cut-outs keep their shape, and a model under a turned
empty stays upright. Every source is made by fixtures/import_fixtures.py."""
import os
import sys
import unittest

import bpy
import numpy as np

from cleanup_helpers import CleanupTest
from plynth import scene
from plynth.materials import read_pixels

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures"))
import import_fixtures as fixtures  # noqa: E402

LEAF_SHARE = float(fixtures.leaf_shape().mean())


# Returns the Principled BSDF of a material.
def principled(material):
    return next(n for n in material.node_tree.nodes if n.bl_idname == "ShaderNodeBsdfPrincipled")


# Says whether a material is in blended mode.
def is_blended(material):
    if hasattr(material, "surface_render_method"):
        return material.surface_render_method == "BLENDED"
    return material.blend_method == "BLEND"


class FixtureTest(CleanupTest):
    # Cleans one fixture .blend into an output folder of its own and returns the result, which must be ok.
    def clean(self, source, name):
        result = self.cleanup(source, "blend", output_folder=self.path("item_" + name))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        return result

    # Returns the material_maps entry of a material by name.
    def entry(self, result, name):
        return next(e for e in result["output"]["material_maps"] if e["name"] == name)

    # Returns the red channel of a map of an output folder, top row first.
    def red(self, name, relative):
        return read_pixels(self.path("item_" + name, *relative.split("/")))[:, :, 0]

    # Opens the asset.blend of an output folder and returns its material of a name.
    def open_material(self, name, material):
        bpy.ops.wm.open_mainfile(filepath=self.path("item_" + name, "asset.blend"))
        return bpy.data.materials[material]


class UprightTests(FixtureTest):
    # A box made upright by what it hangs from keeps its world size after the cleanup, whatever holds it up: a
    # turned empty, a keyed turn of its own, the viewport switch, a constraint, a slanting pair of empties or
    # shape keys.
    def test_model_stays_upright(self):
        for kind in fixtures.MODEL_KINDS:
            with self.subTest(kind=kind):
                source = self.path("in", "model_" + kind, "model.blend")
                expected = fixtures.make_rotated_model(source, kind)
                result = self.clean(source, "model_" + kind)
                size = result["output"]["size"]
                found = (size["width"], size["depth"], size["height"])
                np.testing.assert_allclose(found, expected, atol=1e-3)
                self.assertIn("removed_objects", self.codes(result))

    # The cleaned box has no parent, no animation and no constraint, and its transform is moved into the mesh.
    def test_cleaned_model_is_plain(self):
        source = self.path("in", "model_keyed", "model.blend")
        fixtures.make_rotated_model(source, "keyed")
        self.clean(source, "model_keyed")
        bpy.ops.wm.open_mainfile(filepath=self.path("item_model_keyed", "asset.blend"))
        meshes = scene.mesh_objects(scene.scene_objects())
        self.assertEqual(len(meshes), 1)
        box = meshes[0]
        self.assertIsNone(box.parent)
        self.assertIsNone(box.animation_data)
        self.assertEqual(len(box.constraints), 0)
        self.assertTrue(np.allclose(np.array(box.matrix_world), np.identity(4), atol=1e-6))


class GlassTests(FixtureTest):
    # Clear glass, as a Principled BSDF with full transmission or as a Glass BSDF, gets a see-through opacity map
    # of a quarter, keeps its transmission and IOR in asset.blend and stays blended; the opaque sill stays opaque.
    def test_glass_stays_see_through(self):
        for kind in fixtures.GLASS_KINDS:
            with self.subTest(kind=kind):
                name = "glass_" + kind
                source = self.path("in", name, "model.blend")
                fixtures.make_glass_pane(source, kind)
                result = self.clean(source, name)
                glass = self.entry(result, "Glass")
                opacity = glass["maps"]["opacity"]
                self.assertEqual((opacity["from"], opacity["detail"]), ("value", "0.25"))
                self.assertAlmostEqual(float(self.red(name, opacity["file"]).mean()), 0.25, delta=0.01)
                self.assertNotEqual(glass["preview_maps"]["opacity"]["file"], None)
                self.assertEqual(self.entry(result, "Frame")["maps"]["opacity"]["from"], "none")
                material = self.open_material(name, "Glass")
                shader = principled(material)
                self.assertTrue(shader.inputs["Alpha"].is_linked)
                self.assertAlmostEqual(fixtures.transmission_input(shader).default_value, 1.0, places=4)
                self.assertAlmostEqual(shader.inputs["IOR"].default_value, 1.5, places=4)
                self.assertAlmostEqual(material["plynth_transmission"], 1.0, places=4)
                self.assertTrue(is_blended(material))
                self.assertFalse(is_blended(bpy.data.materials["Frame"]))

    # The glass of asset.fbx, the file the app's preview loads, is see-through too.
    def test_glass_in_preview_fbx(self):
        source = self.path("in", "glass_fbx", "model.blend")
        fixtures.make_glass_pane(source, "principled")
        result = self.clean(source, "glass_fbx")
        self.assertEqual(result["output"]["fbx_file"], "asset.fbx")
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=self.path("item_glass_fbx", "asset.fbx"))
        material = next(m for m in bpy.data.materials if m.name.startswith("Glass"))
        alpha = principled(material).inputs["Alpha"]
        self.assertTrue(alpha.is_linked or alpha.default_value < 0.5)


class LeafTests(FixtureTest):
    # Returns the opacity map of the leaf of a cleaned card as its red channel.
    def leaf_opacity(self, result, name):
        entry = self.entry(result, "Leaf")
        self.assertNotEqual(entry["maps"]["opacity"]["from"], "none")
        return self.red(name, entry["maps"]["opacity"]["file"])

    # Checks that an opacity map holds the leaf: opaque in the middle, clear in the corners, the leaf's share in all.
    def assert_leaf(self, plane):
        size = plane.shape[0]
        middle = plane[size // 2 - 2:size // 2 + 2, size // 2 - 2:size // 2 + 2]
        self.assertGreater(float(middle.min()), 0.9)
        for corner in (plane[:2, :2], plane[:2, -2:], plane[-2:, :2], plane[-2:, -2:]):
            self.assertLess(float(corner.max()), 0.1)
        self.assertAlmostEqual(float(plane.mean()), LEAF_SHARE, delta=0.06)

    # Every way a store leaf card feeds its cut-out gives an opacity map with the leaf in its red channel, a
    # preview copy of it, and a blended material in asset.blend: the image's alpha, a Transparent BSDF mix, an
    # image read through a UV map by name, and a white mask image with the leaf in its alpha.
    def test_leaf_keeps_its_cut_out(self):
        for kind in ("alpha", "mix", "named_uv", "mask"):
            with self.subTest(kind=kind):
                name = "leaf_" + kind
                source = self.path("in", name, "model.blend")
                fixtures.make_leaf_card(source, kind)
                result = self.clean(source, name)
                self.assert_leaf(self.leaf_opacity(result, name))
                preview = self.entry(result, "Leaf")["preview_maps"]["opacity"]["file"]
                self.assert_leaf(self.red(name, preview))
                self.assertNotIn("fully see-through", str(result["warnings"]))
                material = self.open_material(name, "Leaf")
                self.assertTrue(principled(material).inputs["Alpha"].is_linked)
                self.assertTrue(is_blended(material))

    # A leaf read through a UV map by name bakes its colour from the leaf, not from the black of a corner.
    def test_named_uv_bakes_the_leaf_colour(self):
        source = self.path("in", "leaf_uv", "model.blend")
        fixtures.make_leaf_card(source, "named_uv")
        result = self.clean(source, "leaf_uv")
        base = self.entry(result, "Leaf")["maps"]["base_color"]
        self.assertEqual(base["from"], "baked")
        pixels = read_pixels(self.path("item_leaf_uv", *base["file"].split("/")))
        size = pixels.shape[0]
        green = pixels[size // 2, size // 2, :3]
        self.assertGreater(float(green[1]), float(green[0]) + 0.2)

    # An opacity that comes out black all over, which would hide the leaf, is replaced by the alpha of the base
    # color image, and the cleanup says so.
    def test_black_opacity_falls_back_to_base_alpha(self):
        source = self.path("in", "leaf_black", "model.blend")
        fixtures.make_leaf_card(source, "black")
        result = self.clean(source, "leaf_black")
        self.assert_leaf(self.leaf_opacity(result, "leaf_black"))
        self.assertIn("material_approximated", self.codes(result))
        self.assertIn("fully see-through", str(result["warnings"]))


if __name__ == "__main__":
    unittest.main()
