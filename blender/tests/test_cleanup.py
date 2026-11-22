import os
import unittest

import bpy

from cleanup_helpers import CleanupTest
from plynth import scene

EXPECTED_NAMES = ["chair_01", "chair_02", "chair_03", "chair_04", "chair_05", "chair_06"]


class ChairCleanupTests(CleanupTest):
    # Cleans the sample chair once and keeps the result for the checks.
    def setUp(self):
        super().setUp()
        self.chair = self.copy_chair()
        self.result = self.cleanup(os.path.join(self.chair, "chair.obj"), "obj")

    # The result is valid and reports the measured chair.
    def test_result(self):
        self.assertTrue(self.result["ok"], self.result["error"])
        self.assert_result_valid(self.result)
        output = self.result["output"]
        self.assertEqual(output["blend_file"], "asset.blend")
        self.assertEqual(output["fbx_file"], "asset.fbx")
        self.assertIsNone(output["picked_file"])
        self.assertAlmostEqual(output["size"]["width"], 0.45, delta=1e-4)
        self.assertAlmostEqual(output["size"]["depth"], 0.50, delta=1e-4)
        self.assertAlmostEqual(output["size"]["height"], 0.90, delta=1e-4)
        self.assertEqual(output["triangles"], 72)
        self.assertEqual(output["objects"], 6)
        self.assertEqual(output["materials"], ["Fabric", "Wood"])
        self.assertEqual(output["textures"], sorted(
            "textures/maps/%s/%s.png" % (key, channel) for key in ("fabric", "wood")
            for channel in ("base_color", "metallic", "normal", "roughness")))
        self.assertEqual(output["missing_textures"], [])
        self.assertEqual(self.result["warnings"], [])

    # asset.blend holds one collection, six named objects with identity matrices and the chair on the floor.
    def test_cleaned_scene(self):
        objects = self.open_asset()
        self.assertEqual([c.name for c in bpy.context.scene.collection.children], ["chair"])
        self.assertEqual(sorted(o.name for o in objects), EXPECTED_NAMES)
        self.assertEqual([o.data.name for o in sorted(objects, key=lambda o: o.name)], EXPECTED_NAMES)
        for obj in objects:
            for row in range(4):
                for column in range(4):
                    self.assertAlmostEqual(obj.matrix_world[row][column], 1.0 if row == column else 0.0, delta=1e-6)
        low, high = scene.world_bounds(objects)
        self.assertAlmostEqual(low.z, 0.0, delta=1e-6)
        self.assertAlmostEqual((low.x + high.x) / 2, 0.0, delta=1e-6)
        self.assertAlmostEqual((low.y + high.y) / 2, 0.0, delta=1e-6)
        size = high - low
        self.assertAlmostEqual(size.x, 0.45, delta=1e-4)
        self.assertAlmostEqual(size.y, 0.50, delta=1e-4)
        self.assertAlmostEqual(size.z, 0.90, delta=1e-4)
        self.assertEqual(scene.triangle_count(objects), 72)

    # The original order of names decides the numbers, and the backrest is at +Y so the front faces -Y.
    def test_order_and_front(self):
        objects = {o.name: o for o in self.open_asset()}
        back = objects["chair_01"]
        low, high = scene.world_bounds([back])
        self.assertGreater((low.y + high.y) / 2, 0.2)
        seat = objects["chair_06"]
        self.assertEqual([s.material.name for s in seat.material_slots], ["Fabric"])
        self.assertEqual([s.material.name for s in back.material_slots], ["Wood"])

    # The wood image is carried into its map folder and linked by a relative path. The first copy beside it is gone.
    def test_textures(self):
        self.assertTrue(os.path.isfile(self.path("item", "textures", "maps", "wood", "base_color.png")))
        self.assertFalse(os.path.exists(self.path("item", "textures", "chair_wood.png")))
        self.open_asset()
        images = {i.filepath.replace("\\", "/"): i for i in bpy.data.images}
        self.assertIn("//textures/maps/wood/base_color.png", images)
        self.assertIsNone(images["//textures/maps/wood/base_color.png"].packed_file)

    # The output folder holds only asset.blend and textures, and the original files are untouched.
    def test_output_folder(self):
        self.assertEqual(sorted(os.listdir(self.path("item"))), ["asset.blend", "asset.fbx", "textures"])
        self.assertEqual(sorted(os.listdir(self.chair)), ["chair.mtl", "chair.obj", "chair_wood.png"])


class CleanupOptionTests(CleanupTest):
    # A scale of 0.01 shrinks the size and warns that the scale looks wrong.
    def test_scale(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj", scale=0.01)
        self.assertTrue(result["ok"], result["error"])
        size = result["output"]["size"]
        self.assertAlmostEqual(size["width"], 0.0045, delta=1e-6)
        self.assertAlmostEqual(size["depth"], 0.005, delta=1e-6)
        self.assertAlmostEqual(size["height"], 0.009, delta=1e-6)
        self.assertIn("scale_suspicious", self.codes(result))
        low, _high = scene.world_bounds(self.open_asset())
        self.assertAlmostEqual(low.z, 0.0, delta=1e-7)

    # A huge scale warns too.
    def test_scale_large(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj", scale=100)
        self.assertIn("scale_suspicious", self.codes(result))

    # Turning by 90 degrees swaps width and depth and keeps the chair on the floor at the origin.
    def test_rotate(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj", rotate_z=90)
        size = result["output"]["size"]
        self.assertAlmostEqual(size["width"], 0.50, delta=1e-4)
        self.assertAlmostEqual(size["depth"], 0.45, delta=1e-4)
        low, high = scene.world_bounds(self.open_asset())
        self.assertAlmostEqual((low.x + high.x) / 2, 0.0, delta=1e-6)
        self.assertAlmostEqual((low.y + high.y) / 2, 0.0, delta=1e-6)

    # A low triangle limit gives the polycount warning.
    def test_polycount(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj", triangle_warning=10)
        self.assertTrue(result["ok"])
        self.assertIn("high_polycount", self.codes(result))

    # A model without its picture gives one missing_texture warning and a list of the missing names.
    def test_missing_texture(self):
        chair = self.copy_chair()
        os.remove(os.path.join(chair, "chair_wood.png"))
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(self.codes(result), ["missing_texture"])
        self.assertEqual(result["output"]["missing_textures"], ["chair_wood.png"])
        self.assertEqual(result["output"]["textures"], sorted(
            "textures/maps/%s/%s.png" % (key, channel) for key in ("fabric", "wood")
            for channel in ("base_color", "metallic", "normal", "roughness")))
        self.assertEqual(result["output"]["material_maps"][1]["maps"]["base_color"]["from"], "value")
        self.assert_result_valid(result)

    # A picture that lives in another folder than the one the model names is found by its file name.
    def test_texture_found_by_name(self):
        chair = self.copy_chair()
        os.makedirs(os.path.join(chair, "maps", "deep"))
        os.replace(os.path.join(chair, "chair_wood.png"), os.path.join(chair, "maps", "deep", "CHAIR_WOOD.PNG"))
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        self.assertEqual(result["warnings"], [])
        self.assertIn("textures/maps/wood/base_color.png", result["output"]["textures"])
        self.assertEqual(result["output"]["material_maps"][1]["maps"]["base_color"]["detail"], "CHAIR_WOOD.PNG")

    # Progress lines name every step in order.
    def test_progress_steps(self):
        import json
        chair = self.copy_chair()
        self.cleanup(os.path.join(chair, "chair.obj"), "obj")
        lines = [line for line in self.printed.splitlines() if line.startswith("PLYNTH:")]
        steps = [json.loads(line[7:])["progress"]["step"] for line in lines]
        self.assertEqual(steps[:7], ["import_source", "remove_extras", "user_transform", "apply_transforms",
                                     "set_pivot", "name_objects", "collect_textures"])
        self.assertEqual(steps[-3:], ["save_blend", "export_fbx", "render_thumbnail"])

    # Bad params are bad_job and a format that is not registered is unsupported_format.
    def test_bad_params(self):
        chair = self.copy_chair()
        result = self.cleanup(os.path.join(chair, "chair.obj"), "obj", scale=-1)
        self.assertEqual(result["error"]["code"], "bad_job")
        result = self.cleanup(os.path.join(chair, "chair.obj"), "stl")
        self.assertEqual(result["error"]["code"], "unsupported_format")
        self.assert_result_valid(result)

    # An empty OBJ gives no_geometry and a missing file gives import_failed.
    def test_no_geometry_and_missing_file(self):
        os.makedirs(self.path("in"))
        empty = self.path("in", "empty.obj")
        with open(empty, "w") as handle:
            handle.write("# nothing\n")
        result = self.cleanup(empty, "obj")
        self.assertEqual(result["error"]["code"], "no_geometry")
        self.assert_result_valid(result)
        result = self.cleanup(self.path("in", "missing.obj"), "obj")
        self.assertEqual(result["error"]["code"], "import_failed")


if __name__ == "__main__":
    unittest.main()
