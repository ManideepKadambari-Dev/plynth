import json
import unittest

import bpy

from build_helpers import BuildTest, load_params
from plynth.quality import uvs
from plynth.quality._common import polygon_areas

KINDS = ("walls", "floor", "ceiling", "roof", "slab")


# Returns the object name, face index, area, centre, normal and corner count of every face of the building
# surfaces whose UVs have no area, by the same rule the UV quality check uses.
def empty_uv_faces():
    found = []
    for obj in bpy.data.objects:
        if obj.type != "MESH" or obj.get("plynth_kind") not in KINDS:
            continue
        areas = polygon_areas(obj.data)
        if areas is None:
            continue
        uv_area, area = areas
        for index in range(len(area)):
            if uv_area[index] < uvs.EMPTY_UV_AREA and area[index] > uvs.MIN_FACE_AREA:
                polygon = obj.data.polygons[index]
                found.append((obj.name, index, "%.2e" % area[index], tuple(round(x, 3) for x in polygon.center),
                              tuple(round(x, 2) for x in polygon.normal), len(polygon.vertices)))
    return found


class BuildUvTests(BuildTest):
    maxDiff = None

    # Builds a fixture with its own settings, the bevel on, and opens building.blend.
    def build_open(self, name):
        params = load_params(name)
        text = json.dumps(params).replace("@LIBRARY@", self.path("library").replace("\\", "/"))
        params = json.loads(text.replace("@PROJECT@", self.path("project").replace("\\", "/")))
        self.assertGreater(params["settings"]["bevel_width"], 0.0)
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertNotIn("bevel_failed", [p["code"] for p in result["output"]["problems"]])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))

    # Every wall, floor, ceiling, roof and slab face has UVs with real area, also the corners the bevel rounds.
    def test_no_face_without_uv_area(self):
        for name in ("from_core", "from_core_house"):
            with self.subTest(fixture=name):
                self.setUp()
                self.build_open(name)
                self.assertTrue(any(o.get("plynth_kind") == "walls" for o in bpy.data.objects))
                self.assertEqual(empty_uv_faces(), [])


if __name__ == "__main__":
    unittest.main()
