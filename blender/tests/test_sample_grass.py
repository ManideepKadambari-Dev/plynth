import os
import unittest

import numpy as np

import helpers  # noqa: F401
from material_helpers import SAMPLES, MaterialTest

GRASS = os.path.join(SAMPLES, "grass")


class GrassSampleTests(MaterialTest):
    # The grass sample cleans with no warning, keeps its id, license and 2 m tile, and takes its base color,
    # normal and roughness from its own images.
    def test_grass_loads(self):
        result = self.clean_ok(GRASS)
        self.assertEqual(result["warnings"], [])
        output = result["output"]
        self.assertEqual(output["source_info"]["id"], "grass")
        self.assertEqual(output["source_info"]["license"], "cc0")
        self.assertEqual(output["tile_size"], {"width": 2.0, "height": 2.0})
        for channel in ("base_color", "normal", "roughness"):
            self.assertEqual(output["maps"][channel]["from"], "file", channel)
            self.assertTrue(os.path.isfile(self.map_path(result, channel)), channel)

    # The base color is green on the whole: more green than red or blue.
    def test_grass_is_green(self):
        result = self.clean_ok(GRASS)
        pixels = self.pixels(self.map_path(result, "base_color")).astype(np.float64)
        red, green, blue = (pixels[:, :, i].mean() for i in range(3))
        self.assertGreater(green, red)
        self.assertGreater(green, blue)


if __name__ == "__main__":
    unittest.main()
