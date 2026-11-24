"""Tests of the support finder in plain Python, with no Blender: python -I test_support_finder.py"""
import importlib.util
import math
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))


# Loads a module from its file, so no package that needs Blender is imported.
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


finder = load("support_finder", os.path.join(REPO, "blender", "plynth", "support_finder.py"))
models = load("make_sample_models", os.path.join(REPO, "samples", "models", "make_sample_models.py"))


# The triangles of a list of sample boxes, in Blender axes, with the script's offset taken off again.
def box_triangles(boxes):
    triangles = []
    for _, x, y, z, _ in boxes:
        corners = [(px - models.OFFSET[0], py - models.OFFSET[1], pz - models.OFFSET[2])
                   for px, py, pz in models.box_corners(x, y, z)]
        for a, b, c, d in models.FACES:
            triangles.append([corners[a], corners[b], corners[c]])
            triangles.append([corners[a], corners[c], corners[d]])
    return triangles


# The triangles of a UV sphere of a radius standing on z = 0.
def sphere_triangles(radius, rings=16, segments=32):
    def point(ring, seg):
        theta = math.pi * ring / rings
        phi = 2.0 * math.pi * seg / segments
        return (radius * math.sin(theta) * math.cos(phi), radius * math.sin(theta) * math.sin(phi),
                radius + radius * math.cos(theta))
    triangles = []
    for ring in range(rings):
        for seg in range(segments):
            a, b = point(ring, seg), point(ring, seg + 1)
            c, d = point(ring + 1, seg + 1), point(ring + 1, seg)
            if ring > 0:
                triangles.append([a, d, b])
            if ring < rings - 1:
                triangles.append([b, d, c])
    return triangles


class SupportFinderTests(unittest.TestCase):
    def test_shelf_boards_and_top(self):
        supports = finder.find_supports(box_triangles(models.SHELF_BOXES))
        self.assertGreaterEqual(len(supports), 4)
        boards = [s for s in supports if s["clearance"] < 1.0]
        self.assertEqual(len(boards), 4)
        for s in boards:
            self.assertAlmostEqual(s["x1"] - s["x0"], 0.76, places=6)
            self.assertAlmostEqual(s["y1"] - s["y0"], 0.29, places=6)
            self.assertGreater(s["clearance"], 0.4)
            self.assertLess(s["clearance"], 0.45)
        self.assertEqual([round(s["z"], 6) for s in boards], [0.02, 0.47, 0.92, 1.37])
        top = [s for s in supports if s["clearance"] >= 1.0]
        self.assertEqual(len(top), 1)
        self.assertAlmostEqual(top[0]["z"], 1.8, places=6)
        self.assertEqual(top[0]["clearance"], finder.OPEN_CLEARANCE)

    def test_table_has_one_top(self):
        supports = finder.find_supports(box_triangles(models.TABLE_BOXES))
        self.assertEqual(len(supports), 1)
        self.assertAlmostEqual(supports[0]["z"], 0.75, places=6)
        self.assertAlmostEqual(supports[0]["x1"] - supports[0]["x0"], 1.2, places=6)

    def test_sofa_tops_are_measured_but_a_sphere_has_none(self):
        # A sofa's seat, arms and back are flat tops like any other. Set dressing only uses shelves, tables,
        # desks and cabinets as hosts, so these are never dressed.
        sofa = finder.find_supports(box_triangles(models.SOFA_BOXES))
        self.assertTrue(any(abs(s["z"] - 0.42) < 1e-6 for s in sofa))
        self.assertEqual(finder.find_supports(sphere_triangles(0.4)), [])
        self.assertEqual(finder.find_supports(box_triangles(models.TV_BOXES)), [])

    def test_small_and_covered_places_are_left_out(self):
        plate = [("A", (0.0, 0.05), (0.0, 0.5), (0.0, 0.02), "M")]
        self.assertEqual(finder.find_supports(box_triangles(plate)), [])
        low = [("Board", (0.0, 0.5), (0.0, 0.5), (0.0, 0.02), "M"), ("Lid", (0.0, 0.5), (0.0, 0.5), (0.07, 0.09), "M")]
        supports = finder.find_supports(box_triangles(low))
        self.assertEqual(len(supports), 1)
        self.assertAlmostEqual(supports[0]["z"], 0.09, places=6)

    def test_at_most_sixteen_largest_kept(self):
        plates = [("P%d" % i, (0.0, 0.2 + 0.01 * i), (0.0, 0.3), (0.3 * i, 0.3 * i + 0.02), "M") for i in range(20)]
        supports = finder.find_supports(box_triangles(plates))
        self.assertEqual(len(supports), finder.MOST)
        widths = sorted(round(s["x1"] - s["x0"], 6) for s in supports)
        self.assertEqual(widths[0], round(0.2 + 0.01 * 4, 6))
        self.assertEqual([round(s["z"], 6) for s in supports], sorted(round(s["z"], 6) for s in supports))
        for s in supports:
            self.assertEqual(sorted(s), ["clearance", "x0", "x1", "y0", "y1", "z"])

    def test_module_needs_no_blender(self):
        with open(os.path.join(REPO, "blender", "plynth", "support_finder.py"), encoding="utf-8") as f:
            for line in f:
                if line.strip().startswith(("import ", "from ")):
                    self.assertNotIn("bpy", line)
        self.assertNotIn("bpy", sys.modules)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    sys.exit(0 if result.wasSuccessful() else 1)
