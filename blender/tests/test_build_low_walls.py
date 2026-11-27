import copy
import unittest

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

from build_helpers import BuildTest
from low_wall_helpers import low_wall_jobs
from opening_helpers import SETTINGS
from plynth import meshes
from schema_check import load_schema, validate
from test_build_openings import solid_report

REACH = 0.5
BESIDE = 0.15


# Returns the volume the wall solids of a floor have before any cut.
def solid_volume(floor):
    total = 0.0
    for solid in floor["wall_solids"]:
        area = abs(meshes.polygon_area([tuple(p) for p in solid["outline"]["points"]]))
        area -= sum(abs(meshes.polygon_area([tuple(p) for p in hole["points"]])) for hole in solid["holes"])
        total += area * solid["height"]
    return total


# Returns the volume all the openings of a floor should remove.
def opening_volume(floor):
    return sum(abs(meshes.polygon_area([tuple(p) for p in o["outline"]])) * o["thickness"] for o in floor["openings"])


# Returns whether a level ray across the wall of an opening hits the walls. along moves the ray from the
# opening's centre along the wall.
def ray_hits(tree, opening, elevation, along=0.0):
    centre = Vector((opening["centre"][0], opening["centre"][1], 0.0))
    tangent = Vector((opening["tangent"][0], opening["tangent"][1], 0.0))
    right = Vector((opening["right"][0], opening["right"][1], 0.0))
    point = centre + tangent * along - right * REACH
    point.z = elevation + opening["sill"] + opening["height"] / 2
    return tree.ray_cast(point, right, 2 * REACH)[0] is not None


class LowWallTests(BuildTest):
    # Builds a low wall job with the bevel width given and returns the result and the walls object.
    def build_walls(self, data, width):
        params = copy.deepcopy(data["params"])
        params["settings"] = dict(SETTINGS, bevel_width=width)
        result = self.build(params)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        bpy.ops.wm.open_mainfile(filepath=self.path("build", "building.blend"))
        return result, bpy.data.objects["f1_Walls"]

    # Checks the walls are closed, every solid in them points outward and no face is flipped.
    def assert_closed(self, walls, name):
        self.assertTrue(meshes.is_manifold(walls), name)
        volumes, flipped = solid_report(walls)
        self.assertTrue(volumes, name)
        for volume in volumes:
            self.assertGreater(volume, 0.0, name)
        self.assertEqual(flipped, 0, name)

    # Checks a ray through the middle of every opening passes and a ray through the wall beside it is stopped.
    def assert_holes(self, walls, floor, name):
        mesh = walls.data
        tree = BVHTree.FromPolygons([v.co.copy() for v in mesh.vertices], [p.vertices[:] for p in mesh.polygons])
        for opening in floor["openings"]:
            label = "%s %s" % (name, opening["id"])
            self.assertFalse(ray_hits(tree, opening, floor["elevation"]), label)
            self.assertTrue(ray_hits(tree, opening, floor["elevation"], opening["width"] / 2 + BESIDE), label)

    # The jobs match the job schema.
    def test_jobs_valid(self):
        for name, data in low_wall_jobs().items():
            self.assertEqual(validate(data, load_schema("blender-job")), [], name)

    # The owner's case has the 14 openings on its 3 m walls and one 0.08 m wall joined to them.
    def test_owner_case_shape(self):
        floor = low_wall_jobs()["kerb_room"]["params"]["floors"][0]
        self.assertEqual(len(floor["openings"]), 14)
        self.assertEqual(sorted(s["height"] for s in floor["wall_solids"]), [0.08, 3.0])

    # Every opening is cut, each leaves a hole, the walls stay closed and lose the volume of the openings.
    def test_every_opening_cut(self):
        for name, data in low_wall_jobs().items():
            for width in (0.0, 0.01):
                with self.subTest(name=name, width=width):
                    floor = data["params"]["floors"][0]
                    result, walls = self.build_walls(data, width)
                    output = result["output"]
                    self.assertEqual(output["problems"], [], name)
                    self.assertEqual(output["counts"]["openings_cut"], len(floor["openings"]), name)
                    self.assert_closed(walls, name)
                    self.assert_holes(walls, floor, name)
                    if width == 0.0:
                        expected = opening_volume(floor)
                        lost = solid_volume(floor) - meshes.volume(walls)
                        self.assertAlmostEqual(lost, expected, delta=expected * 0.01, msg=name)

    # The side faces keep their wall and side after the cut solid is put back beside the uncut low wall.
    def test_face_values_kept(self):
        _, walls = self.build_walls(low_wall_jobs()["kerb_room"], 0.0)
        names = list(walls["plynth_walls"])
        values = [a.value for a in walls.data.attributes["plynth_wall"].data]
        sides = [a.value for a in walls.data.attributes["plynth_side"].data]
        for x, wall in ((8.1, "w3"), (4.05, "w7")):
            faces = [p for p in walls.data.polygons if p.normal.x > 0.999 and abs(p.center.x - x) < 1e-6]
            self.assertTrue(faces, wall)
            for polygon in faces:
                self.assertEqual(names[values[polygon.index]], wall)
                self.assertEqual(sides[polygon.index], 1, wall)

if __name__ == "__main__":
    unittest.main()
