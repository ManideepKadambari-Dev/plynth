"""Tests that the support finder stays quick on dense models and finds what a plain search finds, in plain
Python with no Blender: python -I test_support_finder_speed.py"""
import importlib.util
import os
import random
import sys
import time
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


# Finds the supports the slow, plain way: join any two touching rectangles of a level and start over, and look
# at every triangle for the clearance.
def plain_supports(triangles):
    rects = []
    for tri in triangles:
        normal = finder._normal(tri)
        if normal is None or normal[2] < finder.UP:
            continue
        z = sum(p[2] for p in tri) / 3.0
        rects.append([min(p[0] for p in tri), min(p[1] for p in tri), max(p[0] for p in tri),
                      max(p[1] for p in tri), z])
    changed = True
    while changed:
        changed = False
        for i in range(len(rects)):
            for j in range(i + 1, len(rects)):
                a, b = rects[i], rects[j]
                if abs(a[4] - b[4]) > finder.SAME_LEVEL:
                    continue
                if a[0] > b[2] + 1e-6 or b[0] > a[2] + 1e-6 or a[1] > b[3] + 1e-6 or b[1] > a[3] + 1e-6:
                    continue
                rects[i] = [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]), max(a[4], b[4])]
                del rects[j]
                changed = True
                break
            if changed:
                break
    out = []
    for x0, y0, x1, y1, z in rects:
        if x1 - x0 < finder.MIN_SIDE - 1e-9 or y1 - y0 < finder.MIN_SIDE - 1e-9:
            continue
        best = finder.OPEN_CLEARANCE
        for tri in triangles:
            low = min(p[2] for p in tri)
            if low <= z + finder.SAME_LEVEL:
                continue
            if max(p[0] for p in tri) <= x0 + 1e-4 or min(p[0] for p in tri) >= x1 - 1e-4:
                continue
            if max(p[1] for p in tri) <= y0 + 1e-4 or min(p[1] for p in tri) >= y1 - 1e-4:
                continue
            best = min(best, low - z)
        if best < finder.MIN_CLEARANCE - 1e-9:
            continue
        out.append({"x0": x0, "y0": y0, "x1": x1, "y1": y1, "z": max(0.0, z), "clearance": best})
    out.sort(key=lambda s: -(s["x1"] - s["x0"]) * (s["y1"] - s["y0"]))
    out = out[:finder.MOST]
    out.sort(key=lambda s: (s["z"], s["x0"], s["y0"]))
    return out


# The twelve triangles of a box between two corners.
def box(x0, y0, z0, x1, y1, z1):
    c = [(x, y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    quads = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    triangles = []
    for a, b, d, e in quads:
        triangles.append([c[a], c[b], c[d]])
        triangles.append([c[a], c[d], c[e]])
    return triangles


# The two triangles of each square of a flat grid at height z.
def grid(count, step, z, x=0.0, y=0.0):
    triangles = []
    for i in range(count):
        for j in range(count):
            px, py = x + i * step, y + j * step
            a, b, c, d = (px, py, z), (px + step, py, z), (px + step, py + step, z), (px, py + step, z)
            triangles += [[a, b, c], [a, c, d]]
    return triangles


class SpeedTests(unittest.TestCase):
    # Random shelves, tables and boxes give the same supports as the plain search.
    def test_same_as_plain_search(self):
        chance = random.Random(7)
        for _ in range(30):
            triangles = []
            for _ in range(chance.randint(1, 12)):
                x0, y0 = chance.uniform(-1, 1), chance.uniform(-1, 1)
                z0 = round(chance.uniform(0, 1.5), 2)
                triangles += box(x0, y0, z0, x0 + chance.uniform(0.05, 0.8), y0 + chance.uniform(0.05, 0.8),
                                 z0 + chance.uniform(0.01, 0.3))
            if chance.random() < 0.5:
                triangles += grid(6, 0.05, 0.75, chance.uniform(-1, 1), chance.uniform(-1, 1))
            self.assertEqual(finder.find_supports(triangles), plain_supports(triangles))

    # A dense top of 400 000 triangles with legs and a shelf is measured in seconds.
    def test_dense_model_is_quick(self):
        triangles = grid(400, 0.003, 0.75, -0.6, -0.6) + grid(100, 0.01, 0.3, -0.5, -0.5)
        for x, y in ((-0.6, -0.6), (0.55, -0.6), (-0.6, 0.55), (0.55, 0.55)):
            triangles += box(x, y, 0.0, x + 0.05, y + 0.05, 0.75)
        started = time.monotonic()
        supports = finder.find_supports(triangles)
        self.assertLess(time.monotonic() - started, 30.0)
        self.assertEqual([round(s["z"], 6) for s in supports], [0.3, 0.75])
        self.assertAlmostEqual(supports[0]["clearance"], 0.45, places=6)
        self.assertEqual(supports[1]["clearance"], finder.OPEN_CLEARANCE)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    sys.exit(0 if result.wasSuccessful() else 1)
