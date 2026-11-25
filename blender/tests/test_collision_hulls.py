import math
import random
import unittest

import bmesh
import bpy
from mathutils import Matrix

import helpers
from plynth import scene
from plynth.building import collision
from plynth.quality import collision as quality_collision


# Returns the vertex positions of a bmesh made by make(bm), then frees it.
def points_of(make):
    bm = bmesh.new()
    make(bm)
    points = [tuple(v.co) for v in bm.verts]
    bm.free()
    return points


# Returns the points of a UV sphere of radius 0.5 with 64 segments and 32 rings.
def sphere_points():
    return points_of(lambda bm: bmesh.ops.create_uvsphere(bm, u_segments=64, v_segments=32, radius=0.5))


# Returns the points of a cylinder of radius 0.4 and height 0.9 with 64 segments.
def cylinder_points():
    return points_of(lambda bm: bmesh.ops.create_cone(bm, cap_ends=True, segments=64, radius1=0.4, radius2=0.4,
                                                      depth=0.9))


# Returns a noisy dense shape: a cushion like box, finely cut, with every point moved by up to a millimetre.
def noisy_points():
    def make(bm):
        bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Diagonal((1.8, 0.8, 0.4, 1.0)))
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=24, use_grid_fill=True)
    generator = random.Random(7)
    return [tuple(c + generator.uniform(-1e-3, 1e-3) for c in point) for point in points_of(make)]


# Returns a flat, finely cut and slightly wavy surface with a thin rim, like a mattress top.
def wavy_points():
    def make(bm):
        bmesh.ops.create_grid(bm, x_segments=120, y_segments=120, size=1.0)
    points = []
    for x, y, z in points_of(make):
        points.append((x, y, 0.3 + 0.002 * math.sin(x * 40.0) * math.cos(y * 37.0)))
        points.append((x, y, 0.0))
    return points


class CollisionHullTests(helpers.TempFolderTest):
    # Builds the hull of the points and checks it the way the export does: closed, convex, small enough for a
    # game engine, and the collision check passes, also once the hull is moved and turned like an item.
    def assert_good_hull(self, points, label):
        mesh = collision.hull_points_mesh("UCX_" + label, points)
        self.assertIsNotNone(mesh, label)
        self.assertLessEqual(len(mesh.vertices), collision.MAX_HULL_POINTS, label)
        self.assertTrue(all(len(p.vertices) == 3 for p in mesh.polygons), label)
        self.assertTrue(quality_collision.is_closed(mesh), label)
        self.assertTrue(quality_collision.is_convex(mesh), label)
        obj = bpy.data.objects.new("UCX_SM_%s_i1_00" % label, mesh.copy())
        bpy.context.scene.collection.objects.link(obj)
        obj.matrix_world = Matrix.Translation((14.0, -9.0, 3.2)) @ Matrix.Rotation(math.radians(37.0), 4, "Z")
        scene.apply_object_transforms([obj])
        ctx = type("Ctx", (), {"state": {"meshes": [], "collision": [obj]}})()
        verdict = quality_collision.run(ctx)
        self.assertEqual(verdict["status"], "pass", (label, verdict["findings"]))
        return mesh

    # A dense UV sphere gets a small convex hull, not the fallback box, that still reaches the sphere's extent
    # on every axis.
    def test_sphere(self):
        mesh = self.assert_good_hull(sphere_points(), "sphere")
        self.assertGreater(len(mesh.vertices), 8)
        bm = bmesh.new()
        bm.from_mesh(mesh)
        self.assertGreater(bm.calc_volume(), 0.8 * 4.0 / 3.0 * math.pi * 0.5 ** 3)
        bm.free()
        for axis in range(3):
            values = [v.co[axis] for v in mesh.vertices]
            self.assertAlmostEqual(max(values), 0.5, delta=1e-3)
            self.assertAlmostEqual(min(values), -0.5, delta=1e-3)

    # A 64 segment cylinder keeps its full height and radius.
    def test_cylinder(self):
        mesh = self.assert_good_hull(cylinder_points(), "cylinder")
        self.assertGreater(len(mesh.vertices), 8)
        heights = [v.co.z for v in mesh.vertices]
        self.assertAlmostEqual(max(heights) - min(heights), 0.9, delta=1e-3)
        self.assertAlmostEqual(max(v.co.x for v in mesh.vertices), 0.4, delta=1e-3)

    # Noisy dense meshes with large nearly flat sides get usable hulls.
    def test_noisy_meshes(self):
        self.assert_good_hull(noisy_points(), "noisy")
        self.assert_good_hull(wavy_points(), "wavy")

    # A small point set, like a roof, is used as it is: the hull of a box keeps its eight corners.
    def test_small_point_set_is_kept(self):
        corners = [(x, y, z) for x in (0.0, 2.0) for y in (0.0, 1.0) for z in (0.0, 0.5)]
        mesh = self.assert_good_hull(corners, "box")
        self.assertEqual(len(mesh.vertices), 8)

    # A flat point set still has no hull.
    def test_flat_points_have_no_hull(self):
        flat = [(x * 0.1, y * 0.1, 0.0) for x in range(20) for y in range(20)]
        self.assertIsNone(collision.hull_points_mesh("UCX_flat", flat))


if __name__ == "__main__":
    unittest.main()
