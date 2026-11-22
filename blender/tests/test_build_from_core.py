import copy
import os
import unittest

import bpy

from build_helpers import BuildTest, load_params
from plynth import meshes


# Returns the area of a ring of points, positive when anticlockwise.
def ring_area(ring):
    return meshes.polygon_area([tuple(p) for p in ring["points"]])


class FromCoreTests(BuildTest):
    # Builds the job core made (two rooms, a curved wall, a rounded corner, all five shapes) with the bevel off.
    def setUp(self):
        super().setUp()
        self.params = copy.deepcopy(load_params("from_core"))
        self.params["settings"]["bevel_width"] = 0.0
        self.result = self.build(self.params)

    # The build succeeds with no problems and the counts match the job.
    def test_counts(self):
        self.assertTrue(self.result["ok"], self.result["error"])
        self.assert_result_valid(self.result)
        output = self.result["output"]
        self.assertEqual(output["problems"], [])
        floor = self.params["floors"][0]
        counts = output["counts"]
        self.assertEqual(counts["openings_cut"], len(floor["openings"]))
        self.assertEqual(counts["rooms"], len(floor["rooms"]))
        self.assertEqual(counts["wall_solids"], len(floor["wall_solids"]))
        fills = len([o for o in floor["openings"] if o["type"] in ("door", "window")])
        self.assertEqual(counts["fills"], fills)
        self.assertEqual(sorted(os.listdir(self.path("build"))), ["building.blend", "building.fbx"])

    # Every room floor has the area core gave it, and the walls lost the volume of the openings.
    def test_geometry(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        floor = self.params["floors"][0]
        for room in floor["rooms"]:
            obj = bpy.data.objects["SM_%s_%s_Floor" % (floor["id"], room["id"])]
            self.assertAlmostEqual(meshes.area(obj), room["area"], delta=1e-3)
            ceiling = bpy.data.objects["SM_%s_%s_Ceiling" % (floor["id"], room["id"])]
            self.assertAlmostEqual(meshes.area(ceiling), room["area"], delta=1e-3)
        solid = floor["wall_solids"][0]
        full = (ring_area(solid["outline"]) + sum(ring_area(h) for h in solid["holes"])) * solid["height"]
        lost_expected = sum(abs(meshes.polygon_area([tuple(p) for p in o["outline"]])) * o["thickness"]
                            for o in floor["openings"])
        walls = bpy.data.objects["SM_%s_Walls" % floor["id"]]
        self.assertTrue(meshes.is_manifold(walls))
        lost = full - meshes.volume(walls)
        self.assertAlmostEqual(lost / lost_expected, 1.0, delta=0.03)
        for opening in floor["openings"]:
            if opening["threshold"]:
                self.assertIn("SM_%s_Thresholds" % floor["id"], bpy.data.objects)
            kind = {"door": "Door", "window": "Window"}.get(opening["type"])
            name = "SM_%s_%s_%s" % (floor["id"], opening["id"], kind)
            self.assertEqual(name in bpy.data.objects, kind is not None, name)

    # With the default bevel the build still has no problems and the walls stay closed.
    def test_with_bevel(self):
        params = copy.deepcopy(load_params("from_core"))
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(result["output"]["problems"], [])
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        self.assertTrue(meshes.is_manifold(bpy.data.objects["SM_f1_Walls"]))


if __name__ == "__main__":
    unittest.main()
