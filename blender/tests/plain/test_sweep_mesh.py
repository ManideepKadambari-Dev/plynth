"""Tests of the sweep mesh helper in plain Python, with no Blender: python -I test_sweep_mesh.py"""
import importlib.util
import math
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MODULE = os.path.normpath(os.path.join(HERE, "..", "..", "plynth", "sweep_mesh.py"))


# Loads the module from its file, so the package (which needs Blender) is not imported.
def load_module():
    spec = importlib.util.spec_from_file_location("sweep_mesh", MODULE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sweep_mesh = load_module()

SQUARE_PROFILE = [(0.0, 0.0), (0.02, 0.0), (0.02, 0.1), (0.0, 0.1)]


# Places a profile (x out to the right of the path, y up) at each point of a flat path, with mitred corners.
def sections_along(path, closed, profile=SQUARE_PROFILE):
    n = len(path)
    out = []
    for k in range(n):
        p = path[k]
        if closed or 0 < k < n - 1:
            a = path[(k - 1) % n]
            b = path[(k + 1) % n]
            d0 = unit((p[0] - a[0], p[1] - a[1]))
            d1 = unit((b[0] - p[0], b[1] - p[1]))
            r0 = (d0[1], -d0[0])
            r1 = (d1[1], -d1[0])
            mid = unit((r0[0] + r1[0], r0[1] + r1[1]))
            stretch = 1.0 / (mid[0] * r1[0] + mid[1] * r1[1])
            side = (mid[0] * stretch, mid[1] * stretch)
        else:
            q = path[1] if k == 0 else path[k]
            o = path[0] if k == 0 else path[k - 1]
            d = unit((q[0] - o[0], q[1] - o[1]))
            side = (d[1], -d[0])
        out.append([(p[0] + side[0] * x, p[1] + side[1] * x, y) for x, y in profile])
    return out


# A 2D vector made one long.
def unit(v):
    size = math.hypot(v[0], v[1])
    return (v[0] / size, v[1] / size)


# Says for each directed edge how often it is used, to find holes and flipped faces.
def edge_uses(faces):
    uses = {}
    for face in faces:
        for i in range(len(face)):
            edge = (face[i], face[(i + 1) % len(face)])
            uses[edge] = uses.get(edge, 0) + 1
    return uses


class SweepMeshTests(unittest.TestCase):
    # Checks that a mesh is closed, each edge used once each way, and holds a positive volume.
    def assert_closed_outward(self, mesh):
        uses = edge_uses(mesh["faces"])
        for (a, b), count in uses.items():
            self.assertEqual(count, 1, "edge %d-%d is used %d times one way" % (a, b, count))
            self.assertEqual(uses.get((b, a)), 1, "edge %d-%d has no face on its other side" % (a, b))
        self.assertGreater(sweep_mesh.signed_volume(mesh["vertices"], mesh["faces"]), 0.0)

    def test_closed_sweep_is_closed_and_faces_out(self):
        path = [(0, 0), (4, 0), (4, 4), (0, 4)]
        mesh = sweep_mesh.sweep_mesh(sections_along(path, True), True)
        self.assertEqual(len(mesh["vertices"]), 16)
        self.assertEqual(len(mesh["faces"]), 16)
        self.assert_closed_outward(mesh)

    def test_reversed_path_still_faces_out(self):
        path = [(0, 0), (0, 4), (4, 4), (4, 0)]
        self.assert_closed_outward(sweep_mesh.sweep_mesh(sections_along(path, True), True))

    def test_open_sweep_gets_caps(self):
        mesh = sweep_mesh.sweep_mesh(sections_along([(0, 0), (2, 0), (2, 3)], False), False)
        self.assertEqual(len(mesh["faces"]), 2 * 4 + 2)
        caps = [f for f in mesh["faces"] if len(f) == 4 and len({i // 4 for i in f}) == 1]
        self.assertEqual(len(caps), 2)
        self.assert_closed_outward(mesh)

    def test_two_section_post(self):
        ring = [(math.cos(a * math.pi / 4) * 0.025, math.sin(a * math.pi / 4) * 0.025) for a in range(8)]
        sections = [[(x, y, 0.0) for x, y in ring], [(x, y, 0.9) for x, y in ring]]
        mesh = sweep_mesh.sweep_mesh(sections, False)
        self.assert_closed_outward(mesh)
        volume = sweep_mesh.signed_volume(mesh["vertices"], mesh["faces"])
        area = 0.5 * 8 * 0.025 * 0.025 * math.sin(math.pi / 4)
        self.assertAlmostEqual(volume, area * 0.9, places=9)

    def test_mitred_corners_keep_the_full_volume(self):
        # A 4 m square ring of 0.02 by 0.1 trim outside the path: its volume is the outer square less the inner.
        path = [(0, 0), (0, 4), (4, 4), (4, 0)]
        mesh = sweep_mesh.sweep_mesh(sections_along(path, True), True)
        self.assert_closed_outward(mesh)
        volume = sweep_mesh.signed_volume(mesh["vertices"], mesh["faces"])
        expected = (4.04 * 4.04 - 4.0 * 4.0) * 0.1
        inner = (4.0 * 4.0 - 3.96 * 3.96) * 0.1
        self.assertTrue(abs(volume - expected) < 1e-9 or abs(volume - inner) < 1e-9, volume)

    def test_uvs_are_in_metres(self):
        mesh = sweep_mesh.sweep_mesh(sections_along([(0, 0), (3, 0)], False), False)
        side_uvs = mesh["uvs"][:4]
        us = sorted({round(u, 9) for face in side_uvs for u, _ in face})
        self.assertEqual(us, [0.0, 3.0])
        vs = sorted({round(v, 9) for face in side_uvs for _, v in face})
        self.assertEqual(vs, [0.0, 0.02, 0.12, 0.14, 0.24])
        for face, uv in zip(mesh["faces"], mesh["uvs"]):
            self.assertEqual(len(face), len(uv))
        cap = mesh["uvs"][4]
        width = max(u for u, _ in cap) - min(u for u, _ in cap)
        height = max(v for _, v in cap) - min(v for _, v in cap)
        self.assertAlmostEqual(sorted([width, height])[0], 0.02, places=9)
        self.assertAlmostEqual(sorted([width, height])[1], 0.1, places=9)

    def test_closed_uvs_run_on_past_the_seam(self):
        path = [(0, 0), (4, 0), (4, 4), (0, 4)]
        mesh = sweep_mesh.sweep_mesh(sections_along(path, True), True)
        top = max(u for face in mesh["uvs"] for u, _ in face)
        self.assertGreater(top, 15.9)

    def test_bad_sections_raise_clear_errors(self):
        with self.assertRaisesRegex(ValueError, "at least two sections"):
            sweep_mesh.sweep_mesh([[(0, 0, 0), (1, 0, 0), (0, 1, 0)]], False)
        with self.assertRaisesRegex(ValueError, "same number of points"):
            sweep_mesh.sweep_mesh([[(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 0, 1), (1, 0, 1)]], False)
        with self.assertRaisesRegex(ValueError, "at least three points"):
            sweep_mesh.sweep_mesh([[(0, 0, 0), (1, 0, 0)], [(0, 0, 1), (1, 0, 1)]], False)
        with self.assertRaisesRegex(ValueError, "finite"):
            sweep_mesh.sweep_mesh([[(0, 0, 0), (1, 0, 0), (0, float("nan"), 0)], [(0, 0, 1), (1, 0, 1), (0, 1, 1)]], False)

    def test_module_needs_no_blender(self):
        with open(MODULE, encoding="utf-8") as f:
            text = f.read()
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("import ") or stripped.startswith("from "):
                self.assertNotIn("bpy", stripped)
                self.assertNotIn("bmesh", stripped)
                self.assertNotIn("mathutils", stripped)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    sys.exit(0 if result.wasSuccessful() else 1)
