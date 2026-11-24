"""The BlenderKit fetch end to end in real Blender, the one case the plain tests (tests/plain) cannot show:
the guard loads from BLENDER_SYSTEM_SCRIPTS before the user's add-on, the worker runs at low integrity with the
fake add-on and the fake helper, as core starts it, and a model and a material land while the scratch user's home
stays unchanged. Run alone on the owner's PC:
    blender -b --factory-startup --python tests/run_tests.py -- test_blenderkit_fetch.py
"""
import json
import os
import unittest

from blenderkit_edge_helpers import MATERIAL, MODEL, EdgeTest


class FetchInBlender(EdgeTest):
    # A model lands from the private download, nothing in the scratch home changes, one request left.
    def test_model(self):
        before = self.home_files()
        result, output = self.fetch(MODEL, {})
        self.assertTrue(result["ok"], json.dumps(result.get("error")))
        self.assertEqual(result["output"]["kind"], "model")
        self.assertEqual(result["output"]["reference"],
                         "https://www.blendkit.com/asset-gallery-detail/%s/" % MODEL.base_id)
        self.assertTrue(os.path.isfile(os.path.join(output, result["output"]["file"])))
        self.assertEqual(self.home_files(), before)

    # A material lands by its name, with nothing in the scratch home changed.
    def test_material(self):
        before = self.home_files()
        result, _output = self.fetch(MATERIAL, {})
        self.assertTrue(result["ok"], json.dumps(result.get("error")))
        self.assertEqual(result["output"]["material"], "Oak floor")
        self.assertEqual(self.home_files(), before)


if __name__ == "__main__":
    unittest.main()
