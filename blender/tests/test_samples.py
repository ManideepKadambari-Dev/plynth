import os
import shutil
import unittest

from cleanup_helpers import CleanupTest
from helpers import REPO_FOLDER
from plynth import scene

MODELS_FOLDER = os.path.join(REPO_FOLDER, "samples", "models")

# Folder name, width, depth, height in metres, triangle count, and the sorted material names.
MODELS = [
    ("sofa", 2.00, 0.90, 0.85, 96, ["Fabric", "Legs"]),
    ("bed", 1.60, 2.10, 1.00, 36, ["Linen", "Wood"]),
    ("table", 1.20, 0.80, 0.75, 60, ["Wood"]),
    ("tv", 1.23, 0.06, 0.71, 24, ["Plastic", "Screen"]),
]


class SampleModelTests(CleanupTest):
    # Copies a sample model folder to a temporary place, cleans it and returns the result.
    def clean_model(self, name):
        target = self.path("in", name)
        shutil.copytree(os.path.join(MODELS_FOLDER, name), target)
        return self.cleanup(os.path.join(target, name + ".obj"), "obj", asset_id=name)

    # Returns the bounds (low, high) of the cleaned objects that use a material.
    def bounds_of_material(self, objects, material):
        chosen = [o for o in objects if any(s.material and s.material.name == material for s in o.material_slots)]
        self.assertTrue(chosen, "no object uses " + material)
        return scene.world_bounds(chosen)

    # Every model cleans to its stated size, sits on the floor at the origin and has the stated materials.
    def test_sizes(self):
        for name, width, depth, height, triangles, materials in MODELS:
            with self.subTest(model=name):
                self.setUp()
                result = self.clean_model(name)
                self.assertTrue(result["ok"], result["error"])
                self.assert_result_valid(result)
                output = result["output"]
                self.assertAlmostEqual(output["size"]["width"], width, delta=1e-4)
                self.assertAlmostEqual(output["size"]["depth"], depth, delta=1e-4)
                self.assertAlmostEqual(output["size"]["height"], height, delta=1e-4)
                self.assertEqual(output["triangles"], triangles)
                self.assertEqual(output["materials"], materials)
                self.assertEqual(output["missing_textures"], [])
                self.assertEqual(result["warnings"], [])
                low, high = scene.world_bounds(self.open_asset())
                self.assertAlmostEqual(low.z, 0.0, delta=1e-6)
                self.assertAlmostEqual((low.x + high.x) / 2, 0.0, delta=1e-6)
                self.assertAlmostEqual((low.y + high.y) / 2, 0.0, delta=1e-6)

    # The sofa back is at +Y, so the front faces -Y, and the arms are at both ends.
    def test_sofa_front(self):
        self.clean_model("sofa")
        objects = self.open_asset()
        low, high = scene.world_bounds(objects)
        middle = (low.y + high.y) / 2
        for obj in objects:
            obj_low, obj_high = scene.world_bounds([obj])
            if obj_high.z - obj_low.z > 0.8:
                self.assertGreater((obj_low.y + obj_high.y) / 2, middle + 0.2)
                self.assertAlmostEqual(obj_high.y, high.y, delta=1e-5)
        self.assertEqual(len([o for o in objects if scene.world_bounds([o])[1].z < 0.7]), 7)

    # The bed headboard is at +Y and is the tallest part.
    def test_bed_front(self):
        self.clean_model("bed")
        objects = self.open_asset()
        low, high = scene.world_bounds(objects)
        tall = [o for o in objects if abs(scene.world_bounds([o])[1].z - 1.0) < 1e-5]
        self.assertEqual(len(tall), 1)
        tall_low, tall_high = scene.world_bounds(tall)
        self.assertAlmostEqual(tall_high.y, high.y, delta=1e-5)
        self.assertGreater((tall_low.y + tall_high.y) / 2, 0.9)

    # The TV screen is a separate object on the -Y face of the body.
    def test_tv_screen(self):
        self.clean_model("tv")
        objects = self.open_asset()
        screen_low, screen_high = self.bounds_of_material(objects, "Screen")
        body_low, body_high = self.bounds_of_material(objects, "Plastic")
        self.assertLess((screen_low.y + screen_high.y) / 2, 0.0)
        self.assertLess((screen_low.y + screen_high.y) / 2, (body_low.y + body_high.y) / 2)
        low, _high = scene.world_bounds(objects)
        self.assertAlmostEqual(screen_low.y, low.y, delta=1e-5)
        self.assertEqual(len(objects), 2)

    # The table has a top and four legs, all in one material.
    def test_table_parts(self):
        self.clean_model("table")
        objects = self.open_asset()
        self.assertEqual(len(objects), 5)
        low, high = scene.world_bounds(objects)
        top = [o for o in objects if abs(scene.world_bounds([o])[1].z - 0.75) < 1e-5]
        self.assertEqual(len(top), 1)
        self.assertAlmostEqual(high.x - low.x, 1.2, delta=1e-4)


if __name__ == "__main__":
    unittest.main()
