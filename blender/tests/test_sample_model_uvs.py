import json
import os
import shutil
import unittest

import numpy as np

from cleanup_helpers import CleanupTest
from helpers import REPO_FOLDER
from plynth.quality import uvs
from plynth.quality._common import polygon_areas

SAMPLES = os.path.join(REPO_FOLDER, "samples")


class SampleModelUvTests(CleanupTest):
    # Every sample model, once cleaned, has UVs on all its faces and no two faces share UV space, by the rules of
    # the export's UV check, so its items pass that check.
    def test_cleaned_models_have_separate_uvs(self):
        with open(os.path.join(SAMPLES, "samples.json"), "r", encoding="utf-8") as handle:
            assets = json.load(handle)["assets"]
        self.assertTrue(assets)
        for asset in assets:
            with self.subTest(model=asset["id"]):
                self.setUp()
                source = os.path.join(SAMPLES, asset["file"])
                target = self.path("in", asset["id"])
                shutil.copytree(os.path.dirname(source), target)
                result = self.cleanup(os.path.join(target, os.path.basename(source)), "obj", asset_id=asset["id"])
                self.assertTrue(result["ok"], result["error"])
                parts = []
                for obj in self.open_asset():
                    if obj.type != "MESH":
                        continue
                    self.assertTrue(len(obj.data.uv_layers), obj.name)
                    uv_area, area = polygon_areas(obj.data)
                    empty = (uv_area < uvs.EMPTY_UV_AREA) & (area > uvs.MIN_FACE_AREA)
                    self.assertEqual(int(np.count_nonzero(empty)), 0, obj.name)
                    parts.append(uvs.uv_triangles(obj.data))
                self.assertTrue(parts)
                self.assertFalse(uvs.has_overlap(np.concatenate(parts)))


if __name__ == "__main__":
    unittest.main()
