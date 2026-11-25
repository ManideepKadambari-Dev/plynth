import json
import math
import os
import shutil
import unittest

import bpy

import helpers
from build_helpers import BuildTest
from plynth import meshes

CORE_COPY = os.path.join(helpers.REPO_FOLDER, "tests", "core", "data", "buildjob", "from_core_finish.json")
MODELS = os.path.join(helpers.REPO_FOLDER, "samples", "models")
BACKGROUND = [0.0549, 0.0667, 0.0863]


class FinishFromCoreTests(BuildTest):
    # Cleans a sample model into library/<id> and returns nothing; the job names library/<id>/asset.blend.
    def make_asset(self, asset_id):
        source = os.path.join(MODELS, asset_id, asset_id + ".obj")
        self.assertTrue(os.path.isfile(source), "%s is missing: run samples/models/make_sample_models.py" % source)
        copy_folder = self.path("src", asset_id)
        shutil.copytree(os.path.dirname(source), copy_folder)
        params = {
            "source": {"file": copy_folder.replace("\\", "/") + "/" + asset_id + ".obj", "format": "obj"},
            "asset_id": asset_id,
            "output_folder": self.path("library", asset_id),
            "scale": 1.0,
            "rotate_z": 0,
            "triangle_warning": 500000,
            "bake_size": 256,
            "thumbnail": {"render": False, "size": 64, "background": BACKGROUND},
        }
        result = self.run_task("cleanup_asset", params, job_id="clean-%s" % asset_id)
        self.assertTrue(result["ok"], result.get("error"))
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return result

    # Builds the finishing job core made, with the door, shelf and books cleaned from the sample models.
    def setUp(self):
        super().setUp()
        if not os.path.isfile(CORE_COPY):
            self.skipTest("from_core_finish.json has not been written yet")
        self.cleaned = {asset_id: self.make_asset(asset_id) for asset_id in ("door", "shelf", "books")}
        with open(CORE_COPY, "r", encoding="utf-8") as handle:
            text = handle.read()
        library = self.path("library").replace("\\", "/")
        text = text.replace("@LIBRARY@", library).replace("@PROJECT@", self.path("project").replace("\\", "/"))
        self.params = json.loads(text)["params"]
        self.params["settings"]["bevel_width"] = 0.0
        self.result = self.build(self.params)

    def test_builds_with_finishing_counts(self):
        self.assertTrue(self.result["ok"], self.result.get("error"))
        counts = self.result["output"]["counts"]
        floor = self.params["floors"][0]
        self.assertEqual(counts.get("trim"), len(floor["trim"]))
        self.assertEqual(counts.get("lights"), len(floor["lights"]))
        self.assertEqual(counts.get("dressing"), len(floor["dressing"]))
        self.assertEqual(counts.get("opening_assets"), 1)
        codes = [p["code"] for p in self.result["output"]["problems"]]
        self.assertNotIn("trim_failed", codes)
        self.assertNotIn("fill_asset_failed", codes)

    def test_trim_is_closed_and_has_its_material(self):
        for entry in self.params["floors"][0]["trim"]:
            obj = bpy.data.objects[entry["object"]]
            self.assertTrue(meshes.is_manifold(obj), entry["object"])
            self.assertGreater(meshes.volume(obj), 0.0, entry["object"])
            self.assertEqual(obj.data.materials[0].name, entry["slot"])

    def test_door_asset_replaces_the_built_fill(self):
        opening = next(o for o in self.params["floors"][0]["openings"] if o.get("asset"))
        obj = bpy.data.objects["SM_door_%s" % opening["id"]]
        self.assertEqual(obj["plynth_kind"], "door")
        for got, want in zip(obj.location, opening["asset"]["location"]):
            self.assertAlmostEqual(got, want, places=5)
        self.assertAlmostEqual(obj.rotation_euler.z, opening["asset"]["rotation_z"], places=5)
        # Meshes parented to the asset empty are its own parts, made real for the FBX; a built fill has no parent.
        fills = [o for o in bpy.data.objects if o.get("plynth_kind") == "door" and o.type == "MESH"
                 and o.get("plynth_id") == opening["id"] and o.parent is None]
        self.assertEqual(fills, [])

    def test_books_and_light_are_placed(self):
        floor = self.params["floors"][0]
        props = [o for o in bpy.data.objects if o.get("plynth_kind") == "dressing" and o.type == "EMPTY"]
        self.assertEqual(len(props), len(floor["dressing"]))
        self.assertEqual({o["plynth_asset"] for o in props}, {"books"})
        parts = [o for o in bpy.data.objects if o.get("plynth_kind") == "dressing" and o.type == "MESH"]
        self.assertTrue(parts and all(o.parent in props for o in parts))
        light = bpy.data.objects["Light_f1_l1"]
        self.assertEqual(light.type, "LIGHT")
        self.assertAlmostEqual(light.location.z, 2.9, places=5)

    def test_shelf_cleanup_measured_its_boards(self):
        supports = self.cleaned["shelf"]["output"].get("supports", [])
        self.assertGreaterEqual(len(supports), 4)
        self.assertEqual(self.cleaned["door"]["output"].get("supports", []), [])
        self.assertTrue(all(math.isfinite(s["clearance"]) for s in supports))


if __name__ == "__main__":
    unittest.main()
