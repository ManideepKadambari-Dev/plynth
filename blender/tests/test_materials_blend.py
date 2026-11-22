import hashlib
import os
import time
import unittest

import numpy as np

import blend_helpers as bh
import helpers  # noqa: F401
from material_helpers import BACKGROUND, MaterialTest
from plynth import registry
from plynth.materials import bakers, sources
from plynth.materials.sources import blend as sources_blend


# Returns the sRGB byte of a linear value.
def srgb_byte(value):
    value = value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
    return int(round(value * 255))


# Returns the sha1 of a file.
def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha1(handle.read()).hexdigest()


class BlendMaterialTest(MaterialTest):
    # Writes materials made by a function into a .blend file in the temporary area and returns its path.
    def make_blend(self, name, *materials):
        path = self.path("in", name + ".blend")
        bh.write_blend(path, list(materials))
        return path

    # Cleans a .blend file. size is the job's bake size; material names one material.
    def clean_blend(self, path, material=None, size=256, **kwargs):
        params = self.clean_params(path, kind="blend", bake_size=size, **kwargs)
        params["source"]["material"] = material
        params["tile_size"] = kwargs.get("tile_size") or {"width": 1.0, "height": 1.0}
        self.out_folder = params["output_folder"]
        return self.run_task("cleanup_material", params)

    # The channel's map pixels as bytes.
    def made(self, result, channel):
        return self.pixels(self.map_path(result, channel))


class RegistryTests(unittest.TestCase):
    # The blend source and the Eevee baker are registry entries, and the direct baker goes first.
    def test_entries_are_registered(self):
        registry.discover(sources)
        registry.discover(bakers)
        self.assertIn("blend", registry.MATERIAL_SOURCES.ids())
        self.assertIn("eevee_render", registry.BAKERS.ids())
        self.assertLess(registry.BAKERS.get("direct").order, registry.BAKERS.get("eevee_render").order)


class GateTests(BlendMaterialTest):
    # Gate B3: node chains of constants come out as exact constant maps, pixel for pixel alike.
    def test_gate_values(self):
        path = self.make_blend("gate", bh.gate_material())
        result = self.clean_blend(path, size=256)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        maps = result["output"]["maps"]
        for channel in ("base_color", "roughness", "metallic", "normal"):
            self.assertEqual(maps[channel]["from"], "baked", channel)
        expected = {
            "base_color": [srgb_byte(0.8), srgb_byte(0.2), srgb_byte(0.1)],
            "roughness": [128] * 3,
            "metallic": [255] * 3,
            "normal": [128, 128, 255],
        }
        report = {}
        for channel, want in expected.items():
            pixels = self.made(result, channel)
            self.assertEqual(pixels.shape[:2], (256, 256))
            error = np.abs(pixels[:, :, :3].astype(int) - np.array(want)).max()
            spread = int((pixels[:, :, :3].reshape(-1, 3).max(axis=0) - pixels[:, :, :3].reshape(-1, 3).min(axis=0)).max())
            report[channel] = (want, int(error), spread)
            self.assertLessEqual(error, 2, "%s: want %s" % (channel, want))
            self.assertEqual(spread, 0, "%s pixels differ" % channel)
        print("GATE B3", report)


class BlendSourceTests(BlendMaterialTest):
    # Image textures linked straight in are copied by the direct baker with origin file.
    def test_image_textures_use_direct(self):
        files = {}
        for index, channel in enumerate(("base_color", "roughness", "metallic", "normal")):
            files[channel] = self.path("src", channel + ".png")
            bh.write_noise_png(files[channel], seed=index + 1)
        path = self.make_blend("images", bh.image_material("brick", files))
        before = digest(path)
        result = self.clean_blend(path)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        for channel in files:
            self.assertEqual(result["output"]["maps"][channel]["from"], "file", channel)
            self.assertEqual(digest(self.map_path(result, channel)), digest(files[channel]), channel)
        self.assertEqual(result["output"]["picked_material"], "brick")
        self.assertEqual(digest(path), before)

    # An image next to the .blend file by a relative path is found.
    def test_relative_image_path(self):
        source = self.path("in", "tex", "color.png")
        bh.write_noise_png(source)
        material, principled = bh.new_material("rel")
        image = bh.image_node(material.node_tree, source, "sRGB")
        material.node_tree.links.new(image.outputs["Color"], principled.inputs["Base Color"])
        image.image.filepath = "//tex/color.png"
        path = self.make_blend("rel", material)
        result = self.clean_blend(path)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["maps"]["base_color"]["from"], "file")

    # A packed image is written out and used as a file.
    def test_packed_image(self):
        source = self.path("src", "packed.png")
        data = bh.write_noise_png(source)
        material, principled = bh.new_material("packed")
        image = bh.image_node(material.node_tree, source, "sRGB")
        image.image.pack()
        material.node_tree.links.new(image.outputs["Color"], principled.inputs["Base Color"])
        path = self.make_blend("packed", material)
        os.remove(source)
        result = self.clean_blend(path)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["maps"]["base_color"]["from"], "file")
        self.assertTrue(np.array_equal(self.made(result, "base_color")[:, :, :3], data))

    # Plain values are written as images, as origin value.
    def test_plain_values(self):
        path = self.make_blend("plain", bh.plain_material("paint"))
        result = self.clean_blend(path)
        self.assertTrue(result["ok"], result["error"])
        maps = result["output"]["maps"]
        for channel in ("base_color", "roughness", "metallic", "normal"):
            self.assertEqual(maps[channel]["from"], "value", channel)
        self.assertEqual(self.made(result, "base_color")[0, 0, :3].tolist(),
                         [srgb_byte(0.5), srgb_byte(0.25), srgb_byte(0.1)])
        self.assertEqual(self.made(result, "roughness")[0, 0, 0], 77)
        self.assertEqual(self.made(result, "metallic")[0, 0, 0], 178)

    # A Noise texture into Base Color is baked at the job's size, and its tile has seams.
    def test_noise_is_baked_with_seams(self):
        path = self.make_blend("noise", bh.noise_material("clouds"))
        result = self.clean_blend(path, size=512)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(result["output"]["maps"]["base_color"]["from"], "baked")
        self.assertEqual(result["output"]["maps"]["roughness"]["from"], "value")
        self.assertEqual(result["output"]["texture_size"], {"width": 512, "height": 512})
        pixels = self.made(result, "base_color")
        self.assertGreater(pixels[:, :, 0].std(), 5.0)
        self.assertIn("visible_seams", self.codes(result))

    # Two materials with no name given is an error that lists both; a name picks one.
    def test_several_materials(self):
        path = self.make_blend("two", bh.plain_material("alpha_mat"), bh.plain_material("beta_mat", roughness=0.9))
        result = self.clean_blend(path)
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "several_materials")
        self.assertIn("alpha_mat", result["error"]["message"])
        self.assertIn("beta_mat", result["error"]["message"])
        picked = self.clean_blend(path, material="beta_mat")
        self.assertTrue(picked["ok"], picked["error"])
        self.assertEqual(picked["output"]["picked_material"], "beta_mat")
        self.assertEqual(self.made(picked, "roughness")[0, 0, 0], 229)

    # A name the file does not have is import_failed.
    def test_unknown_material(self):
        path = self.make_blend("one", bh.plain_material("only"))
        result = self.clean_blend(path, material="nope")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "import_failed")

    # A file that is not a .blend, or is missing, is import_failed.
    def test_bad_file(self):
        result = self.clean_blend(self.path("in", "missing.blend"))
        self.assertEqual(result["error"]["code"], "import_failed")

    # The source file is not changed and nothing remains in the session afterwards.
    def test_source_unchanged_and_session_clean(self):
        import bpy
        path = self.make_blend("keep", bh.noise_material("keepme"))
        before = digest(path)
        materials = len(bpy.data.materials)
        result = self.clean_blend(path)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(digest(path), before)
        self.assertEqual(len(bpy.data.materials), materials)

    # The material of the sample sofa's cleaned asset.blend can be taken, and the file is not changed.
    def test_material_of_the_sample_sofa(self):
        import bpy
        sofa = os.path.join(helpers.REPO_FOLDER, "samples", "models", "sofa", "sofa.obj")
        cleaned = self.run_task("cleanup_asset", {
            "source": {"file": sofa.replace("\\", "/"), "format": "obj"}, "asset_id": "sofa",
            "output_folder": self.path("item"), "scale": 1.0, "rotate_z": 0, "triangle_warning": 500000,
            "thumbnail": {"render": False, "size": 64, "background": BACKGROUND}})
        self.assertTrue(cleaned["ok"], cleaned["error"])
        path = self.path("item", "asset.blend")
        bpy.ops.wm.read_factory_settings(use_empty=True)
        before = digest(path)
        names = sources_blend.material_names(path)
        self.assertGreaterEqual(len(names), 1)
        result = self.clean_blend(path, material=names[0], output_folder=self.path("out"))
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        self.assertEqual(result["output"]["picked_material"], names[0])
        self.assertEqual(digest(path), before)
        self.assertEqual(self.made(result, "base_color").shape[0] > 0, True)


class BakeAccuracyTests(BlendMaterialTest):
    # An image behind a Mapping node is baked with the same orientation and pixels as the image, and a
    # 1024 px bake takes a few seconds at most.
    def test_mapped_image_keeps_orientation(self):
        source = self.path("src", "mapped.png")
        data = bh.write_noise_png(source, size=16, seed=9)
        material, principled = bh.new_material("mapped")
        tree = material.node_tree
        image = bh.image_node(tree, source, "sRGB", interpolation="Closest")
        coords = bh.node(tree, "ShaderNodeTexCoord")
        mapping = bh.node(tree, "ShaderNodeMapping")
        tree.links.new(coords.outputs["UV"], mapping.inputs["Vector"])
        tree.links.new(mapping.outputs["Vector"], image.inputs["Vector"])
        tree.links.new(image.outputs["Color"], principled.inputs["Base Color"])
        path = self.make_blend("mapped", material)
        started = time.monotonic()
        result = self.clean_blend(path, size=1024)
        seconds = time.monotonic() - started
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["maps"]["base_color"]["from"], "baked")
        baked = self.made(result, "base_color")[:, :, :3]
        want = np.repeat(np.repeat(data, 64, axis=0), 64, axis=1)
        wrong = int((np.abs(baked.astype(int) - want.astype(int)).max(axis=2) > 2).sum())
        print("BAKE 1024 px: %.2f s for the whole job, %d of %d pixels differ" % (seconds, wrong, 1024 * 1024))
        self.assertEqual(wrong, 0)
        self.assertLess(seconds, 30.0)


if __name__ == "__main__":
    unittest.main()
